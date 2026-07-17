from __future__ import annotations

import asyncio
from dataclasses import asdict, dataclass
from html import escape
import os
from pathlib import Path
import socket
import threading
import time
import uuid
from urllib.parse import urljoin
import xml.etree.ElementTree as ET

import requests

from adhan_config import DATA_DIR


SSDP_ADDRESS = ("239.255.255.250", 1900)
DLNA_DISCOVERY_TARGETS = (
    "urn:schemas-upnp-org:device:MediaRenderer:1",
    "urn:schemas-upnp-org:device:ZonePlayer:1",
)
DLNA_TIMEOUT = float(os.getenv("ADHAN_DLNA_TIMEOUT", "5"))
AIRPLAY_TIMEOUT = int(os.getenv("ADHAN_AIRPLAY_TIMEOUT", "5"))
AIRPLAY_STORAGE_FILE = DATA_DIR / "airplay_credentials.json"


@dataclass(frozen=True)
class DlnaDevice:
    identifier: str
    name: str
    model: str
    location: str
    av_transport_url: str
    rendering_control_url: str


def _xml_text(element: ET.Element, name: str) -> str:
    for item in element.iter():
        if item.tag.rsplit("}", 1)[-1] == name and item.text:
            return item.text.strip()
    return ""


def _service_value(device: ET.Element, service_name: str, value_name: str) -> str:
    for service in device.iter():
        if service.tag.rsplit("}", 1)[-1] != "service":
            continue
        service_type = _xml_text(service, "serviceType")
        if service_name not in service_type:
            continue
        return _xml_text(service, value_name)
    return ""


def describe_dlna_device(location: str) -> DlnaDevice:
    try:
        response = requests.get(location, timeout=DLNA_TIMEOUT)
        response.raise_for_status()
        root = ET.fromstring(response.content)
    except (requests.RequestException, ET.ParseError) as exc:
        raise RuntimeError(f"Could not read DLNA speaker description at {location}") from exc

    device = next(
        (
            item
            for item in root.iter()
            if item.tag.rsplit("}", 1)[-1] == "device"
            and "MediaRenderer" in _xml_text(item, "deviceType")
        ),
        None,
    )
    if device is None:
        raise RuntimeError("The selected network device is not a DLNA MediaRenderer")

    av_transport = _service_value(device, "AVTransport", "controlURL")
    if not av_transport:
        raise RuntimeError("The selected DLNA speaker does not expose AVTransport")
    rendering_control = _service_value(device, "RenderingControl", "controlURL")
    identifier = _xml_text(device, "UDN") or location
    return DlnaDevice(
        identifier=identifier,
        name=_xml_text(device, "friendlyName") or "DLNA speaker",
        model=_xml_text(device, "modelName"),
        location=location,
        av_transport_url=urljoin(location, av_transport),
        rendering_control_url=urljoin(location, rendering_control) if rendering_control else "",
    )


def discover_dlna_devices(timeout: float = 2.5) -> list[dict]:
    locations: set[str] = set()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    try:
        sock.settimeout(0.25)
        for target in DLNA_DISCOVERY_TARGETS:
            request = (
                "M-SEARCH * HTTP/1.1\r\n"
                f"HOST: {SSDP_ADDRESS[0]}:{SSDP_ADDRESS[1]}\r\n"
                'MAN: "ssdp:discover"\r\n'
                "MX: 2\r\n"
                f"ST: {target}\r\n\r\n"
            ).encode("ascii")
            sock.sendto(request, SSDP_ADDRESS)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                payload, _ = sock.recvfrom(8192)
            except socket.timeout:
                continue
            headers: dict[str, str] = {}
            for line in payload.decode("iso-8859-1", errors="replace").split("\r\n")[1:]:
                if ":" in line:
                    name, value = line.split(":", 1)
                    headers[name.strip().lower()] = value.strip()
            if headers.get("location"):
                locations.add(headers["location"])
    finally:
        sock.close()

    devices: list[dict] = []
    identifiers: set[str] = set()
    for location in sorted(locations):
        try:
            device = describe_dlna_device(location)
        except RuntimeError:
            continue
        if device.identifier in identifiers:
            continue
        identifiers.add(device.identifier)
        devices.append(asdict(device))
    return devices


def _soap(url: str, service: str, action: str, body: str) -> None:
    envelope = (
        '<?xml version="1.0" encoding="utf-8"?>'
        '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
        's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
        f"<s:Body><u:{action} xmlns:u=\"urn:schemas-upnp-org:service:{service}:1\">"
        f"{body}</u:{action}></s:Body></s:Envelope>"
    )
    response = requests.post(
        url,
        data=envelope.encode("utf-8"),
        headers={
            "Content-Type": 'text/xml; charset="utf-8"',
            "SOAPACTION": f'"urn:schemas-upnp-org:service:{service}:1#{action}"',
        },
        timeout=DLNA_TIMEOUT,
    )
    response.raise_for_status()


def play_dlna(location: str, media_url: str, volume: float) -> DlnaDevice:
    device = describe_dlna_device(location)
    if device.rendering_control_url:
        try:
            _soap(
                device.rendering_control_url,
                "RenderingControl",
                "SetVolume",
                "<InstanceID>0</InstanceID><Channel>Master</Channel>"
                f"<DesiredVolume>{round(max(0, min(1, volume)) * 100)}</DesiredVolume>",
            )
        except (requests.RequestException, RuntimeError):
            pass
    _soap(
        device.av_transport_url,
        "AVTransport",
        "SetAVTransportURI",
        "<InstanceID>0</InstanceID>"
        f"<CurrentURI>{escape(media_url)}</CurrentURI>"
        "<CurrentURIMetaData></CurrentURIMetaData>",
    )
    _soap(
        device.av_transport_url,
        "AVTransport",
        "Play",
        "<InstanceID>0</InstanceID><Speed>1</Speed>",
    )
    return device


class _AirPlayRuntime:
    def __init__(self) -> None:
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(
            target=self.loop.run_forever,
            name="adhancron-airplay",
            daemon=True,
        )
        self.thread.start()
        self.pairings: dict[str, object] = {}

    def run(self, coroutine, timeout: float = 60):
        future = asyncio.run_coroutine_threadsafe(coroutine, self.loop)
        return future.result(timeout=timeout)


_airplay_runtime = _AirPlayRuntime()


async def _airplay_storage():
    try:
        from pyatv.storage.file_storage import FileStorage
    except ImportError as exc:
        raise RuntimeError("AirPlay support is not installed") from exc
    AIRPLAY_STORAGE_FILE.parent.mkdir(parents=True, exist_ok=True)
    storage = FileStorage(str(AIRPLAY_STORAGE_FILE), asyncio.get_running_loop())
    await storage.load()
    return storage


def _airplay_identifier(config) -> str:
    return config.identifier or str(config.address)


async def _discover_airplay_async() -> list[dict]:
    try:
        import pyatv
        from pyatv.const import PairingRequirement, Protocol
    except ImportError as exc:
        raise RuntimeError("AirPlay support is not installed") from exc
    storage = await _airplay_storage()
    configs = await pyatv.scan(
        asyncio.get_running_loop(),
        timeout=AIRPLAY_TIMEOUT,
        protocol={Protocol.AirPlay, Protocol.RAOP},
        storage=storage,
    )
    devices = []
    identifiers: set[str] = set()
    for config in configs:
        identifier = _airplay_identifier(config)
        if identifier in identifiers:
            continue
        identifiers.add(identifier)
        services = [
            service
            for protocol in (Protocol.RAOP, Protocol.AirPlay)
            if (service := config.get_service(protocol)) is not None
        ]
        pairing_required = any(
            service.pairing == PairingRequirement.Mandatory and not service.credentials
            for service in services
        )
        devices.append(
            {
                "identifier": identifier,
                "name": config.name,
                "model": str(config.device_info.model or ""),
                "address": str(config.address),
                "pairing_required": pairing_required,
            }
        )
    return devices


def discover_airplay_devices() -> list[dict]:
    return _airplay_runtime.run(_discover_airplay_async(), AIRPLAY_TIMEOUT + 10)


async def _find_airplay(identifier: str, storage):
    import pyatv
    from pyatv.const import Protocol

    configs = await pyatv.scan(
        asyncio.get_running_loop(),
        timeout=AIRPLAY_TIMEOUT,
        identifier=identifier,
        protocol={Protocol.AirPlay, Protocol.RAOP},
        storage=storage,
    )
    if not configs:
        raise RuntimeError("The saved AirPlay speaker was not found on this network")
    return configs[0]


async def _start_airplay_pairing_async(identifier: str) -> dict:
    import pyatv
    from pyatv.const import PairingRequirement, Protocol

    storage = await _airplay_storage()
    config = await _find_airplay(identifier, storage)
    candidates = []
    for protocol in (Protocol.RAOP, Protocol.AirPlay):
        service = config.get_service(protocol)
        if service is not None and service.pairing in {
            PairingRequirement.Mandatory,
            PairingRequirement.Optional,
        }:
            candidates.append((protocol, service))
    if not candidates or all(service.credentials for _, service in candidates):
        return {"paired": True, "pairing_required": False}

    protocol, _ = next(
        ((protocol, service) for protocol, service in candidates if not service.credentials),
        candidates[0],
    )
    pairing = await pyatv.pair(
        config,
        protocol,
        asyncio.get_running_loop(),
        storage=storage,
    )
    await pairing.begin()
    session_id = uuid.uuid4().hex
    _airplay_runtime.pairings[session_id] = (pairing, storage)
    return {
        "paired": False,
        "pairing_required": True,
        "session_id": session_id,
        "device_provides_pin": pairing.device_provides_pin,
        "protocol": protocol.name,
    }


def start_airplay_pairing(identifier: str) -> dict:
    return _airplay_runtime.run(
        _start_airplay_pairing_async(identifier),
        AIRPLAY_TIMEOUT + 15,
    )


async def _finish_airplay_pairing_async(session_id: str, pin: int) -> dict:
    entry = _airplay_runtime.pairings.pop(session_id, None)
    if entry is None:
        raise RuntimeError("The AirPlay pairing session expired; start pairing again")
    pairing, storage = entry
    try:
        pairing.pin(pin)
        await pairing.finish()
        if not pairing.has_paired:
            raise RuntimeError("AirPlay pairing was not accepted")
        await storage.save()
        return {"paired": True}
    finally:
        await pairing.close()


def finish_airplay_pairing(session_id: str, pin: str) -> dict:
    try:
        pin_number = int(pin)
    except ValueError as exc:
        raise RuntimeError("Enter the numeric PIN shown by the AirPlay speaker") from exc
    return _airplay_runtime.run(
        _finish_airplay_pairing_async(session_id, pin_number),
        45,
    )


async def _play_airplay_async(identifier: str, media_url: str, volume: float) -> None:
    import pyatv
    from pyatv.const import Protocol

    storage = await _airplay_storage()
    config = await _find_airplay(identifier, storage)
    protocol = Protocol.RAOP if config.get_service(Protocol.RAOP) else Protocol.AirPlay
    atv = await pyatv.connect(
        config,
        asyncio.get_running_loop(),
        protocol=protocol,
        storage=storage,
    )
    try:
        try:
            await atv.audio.set_volume(max(0, min(1, volume)) * 100)
        except Exception:
            pass
        if protocol == Protocol.RAOP:
            await atv.stream.stream_file(media_url)
        else:
            await atv.stream.play_url(media_url)
    finally:
        atv.close()


def play_airplay(identifier: str, media_url: str, volume: float) -> None:
    if not identifier:
        raise RuntimeError("Choose an AirPlay speaker")
    _airplay_runtime.run(
        _play_airplay_async(identifier, media_url, volume),
        15 * 60,
    )
