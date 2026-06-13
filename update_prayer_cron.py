import json
import subprocess

PRAYER_TIMES_FILE = '/app/prayer_times.json'

def load_prayer_times():
    with open(PRAYER_TIMES_FILE) as file:
        return json.load(file)

def update_cron_job(prayer_name, time):
    cron_command = f'python3 /app/trigger_ha.py http://YOUR_DOCKER_HOST_IP:8000/audio/{prayer_name}.mp3 1.0'
    # Extract necessary fields to form a cron job
    hour, minute = time.split(':')
    cron_expression = f'{minute} {hour} * * * '
    full_command = f'{cron_expression} cd /app && {cron_command} >> cron.log 2>&1'
    print(f'Updating Cron Jobs for {prayer_name} at {time}')
    # Here you would usually execute the command to update the cron job using subprocess
    # subprocess.run(['crontab', '-l'], capture_output=True)
    # And then write the new cron configuration to crontab

# Main function to update all cron jobs based on prayer times
if __name__ == '__main__':
    prayer_times = load_prayer_times()
    
    for month, days in prayer_times['2026'].items():
        for day, times in days.items():
            for prayer, time in times.items():
                update_cron_job(prayer, time)
