import requests
import json
import asyncio
from gmqtt import Client as MQTTClient
from datetime import datetime, timedelta, timezone


base_url = "https://platform.fabriq.space/api/series/os-atm:12029:electricity/"


access_token = "8xVK2v3uSQMZf9rohVDZEUmBpNwfQF"


headers = {
    "Authorization": f"Bearer {access_token}"
}


mqtt_broker = "mqtt.cetools.org"
mqtt_port = 1884
mqtt_topic_electricity = "student/CASA0022/zczqhw8/Electricity"
mqtt_topic_emissions = "student/CASA0022/zczqhw8/Carbon_Emissions"
mqtt_username = "student"
mqtt_password = "ce2021-mqtt-forget-whale"


interval = 600


emissions_factor = 0.19338


def fetch_data_from_api(url, headers):
    try:
        response = requests.get(url, headers=headers)
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"Error: {e}")
        return None


def filter_last_year_data(data):
    if isinstance(data, dict) and data.get("data_points"):
        now = datetime.now(timezone.utc)
        one_year_ago = now - timedelta(days=365)
        filtered_data = {}
        for point in data["data_points"]:
            try:
                timestamp = datetime.strptime(point, "%Y-%m-%dT%H:%M:%S").replace(tzinfo=timezone.utc)
                year_month = timestamp.strftime("%Y-%m")
                if timestamp >= one_year_ago:
                    if year_month not in filtered_data:
                        filtered_data[year_month] = 0
                    filtered_data[year_month] += data["data_points"][point]
            except KeyError:
                print(f"data lack of 'timestamp' : {point}")
        return filtered_data
    else:
        print("there is no 'data_points'")
        return {}


def filter_for_current_year(data):
    
    filtered_data = {k: v for k, v in data.items() if k.startswith('2024')}
    return filtered_data


async def send_data_to_mqtt(broker, port, topic_electricity, topic_emissions, username, password, data):
    client = MQTTClient("client_id")
    client.set_auth_credentials(username, password)
    await client.connect(broker, port)
    
    
    filtered_data = filter_for_current_year(data)

    for month, value in filtered_data.items():
        
        sub_topic_electricity = f"{topic_electricity}/{month}"
        client.publish(sub_topic_electricity, str(value))  
        
        
        carbon_emission = value * emissions_factor
        sub_topic_emissions = f"{topic_emissions}/{month}"
        client.publish(sub_topic_emissions, str(carbon_emission))
    
    await asyncio.sleep(1)
    await client.disconnect()


async def periodic_task(interval):
    while True:
        data = fetch_data_from_api(base_url, headers)
        if data:
            filtered_data = filter_last_year_data(data)
            if filtered_data:
                await send_data_to_mqtt(mqtt_broker, mqtt_port, mqtt_topic_electricity, mqtt_topic_emissions, mqtt_username, mqtt_password, filtered_data)
                print("Monthly data has been sent to MQTT")
            else:
                print("Didn't find data")
        else:
            print("Cann't get data from API")
        
        await asyncio.sleep(interval)  

if __name__ == "__main__":
    asyncio.get_event_loop().run_until_complete(periodic_task(interval))
