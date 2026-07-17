from __future__ import annotations

import unittest
from unittest import mock

import network_speakers


DEVICE_XML = b"""<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
  <device>
    <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>
    <friendlyName>Living Room</friendlyName>
    <modelName>Sonos One</modelName>
    <UDN>uuid:speaker-1</UDN>
    <serviceList>
      <service>
        <serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>
        <controlURL>/MediaRenderer/AVTransport/Control</controlURL>
      </service>
      <service>
        <serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>
        <controlURL>/MediaRenderer/RenderingControl/Control</controlURL>
      </service>
    </serviceList>
  </device>
</root>"""


class FakeResponse:
    def __init__(self, content=b"", status_code=200):
        self.content = content
        self.status_code = status_code

    def raise_for_status(self):
        if self.status_code >= 400:
            raise network_speakers.requests.HTTPError(str(self.status_code))


class NetworkSpeakerTests(unittest.TestCase):
    def test_describes_dlna_renderer_and_resolves_control_urls(self):
        with mock.patch.object(
            network_speakers.requests,
            "get",
            return_value=FakeResponse(DEVICE_XML),
        ):
            device = network_speakers.describe_dlna_device(
                "http://192.168.1.40:1400/xml/device_description.xml"
            )
        self.assertEqual(device.identifier, "uuid:speaker-1")
        self.assertEqual(device.name, "Living Room")
        self.assertEqual(device.model, "Sonos One")
        self.assertEqual(
            device.av_transport_url,
            "http://192.168.1.40:1400/MediaRenderer/AVTransport/Control",
        )

    def test_dlna_play_sets_volume_uri_and_starts_transport(self):
        posts = []

        def fake_post(url, data=None, headers=None, timeout=None):
            posts.append((url, data.decode("utf-8"), headers))
            return FakeResponse()

        with mock.patch.object(
            network_speakers.requests,
            "get",
            return_value=FakeResponse(DEVICE_XML),
        ), mock.patch.object(network_speakers.requests, "post", side_effect=fake_post):
            network_speakers.play_dlna(
                "http://192.168.1.40:1400/xml/device_description.xml",
                "http://192.168.1.16:8090/audio/adhan_final.mp3?x=1&y=2",
                0.8,
            )

        self.assertEqual(len(posts), 3)
        self.assertIn("SetVolume", posts[0][1])
        self.assertIn("<DesiredVolume>80</DesiredVolume>", posts[0][1])
        self.assertIn("SetAVTransportURI", posts[1][1])
        self.assertIn("?x=1&amp;y=2", posts[1][1])
        self.assertIn("Play", posts[2][1])


if __name__ == "__main__":
    unittest.main()
