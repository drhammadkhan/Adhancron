import json
from datetime import datetime, timedelta

def schedule_reminders():
    with open('projects/ramadhan_esp32/schedule.json', 'r') as f:
        schedule = json.load(f)
    
    now = datetime.now()
    # We'll schedule reminders for the next 7 days
    count = 0
    for day in schedule:
        date_str = day['date']
        iftar_str = day['iftar']
        
        # Parse time
        iftar_time = datetime.strptime(f"{date_str} {iftar_str}", "%Y-%m-%d %H:%M")
        reminder_time = iftar_time - timedelta(minutes=5)
        
        if reminder_time > now and count < 7:
            # We will print the commands to be run by the agent
            # reminder_time.isoformat()
            print(f"ISO: {reminder_time.isoformat()}")
            print(f"DATE: {date_str}")
            print(f"IFTAR: {iftar_str}")
            count += 1

if __name__ == "__main__":
    schedule_reminders()
