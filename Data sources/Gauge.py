import requests
import json
from datetime import datetime, timedelta, timezone
import paho.mqtt.client as mqtt
import time
import threading
from selenium import webdriver
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.common.by import By
from bs4 import BeautifulSoup
import re


interval = 600


mqtt_broker = "mqtt.cetools.org"
mqtt_port = 1884
mqtt_topic_energy = "student/CASA0022/zczqhw8/Solar_energy"
mqtt_topic_electricity = "student/CASA0022/zczqhw8/Electricity/2024-08"
mqtt_topic_renewable_perc = "student/CASA0022/zczqhw8/Renewable_perc"
mqtt_topic_renewables_from_grid = "student/CASA0022/zczqhw8/Renewables_from_grid"
mqtt_topic_net_zero_ratio = "student/CASA0022/zczqhw8/Net_zero_ratio"
mqtt_username = "student"
mqtt_password = "ce2021-mqtt-forget-whale"


driver_path = '/Users/wanghaoming/Downloads/chromedriver-mac-arm64/chromedriver'  
url = 'https://monitoringpublic.solaredge.com/solaredge-web/p/kiosk?guid=2dd7fbae-b3f0-417c-b67b-e2759946a622'


initial_electricity_value = None
renewable_perc_value = None
energy_this_month_value = None
renewables_from_grid = None

def fetch_data(base_url, from_date, to_date, postcode):
    url = f"{base_url}/{from_date}/{to_date}/postcode/{postcode}"
    response = requests.get(url, headers={"Accept": "application/json"})
    return response.json()

def split_date_range(start_date, end_date, delta_days=13):
    current_date = start_date
    while current_date < end_date:
        next_date = min(current_date + timedelta(days=delta_days), end_date)
        yield current_date, next_date
        current_date = next_date

def calculate_averages(fuel_perc):
    average_generation_mix = []
    for fuel, percs in fuel_perc.items():
        average = sum(percs) / len(percs) if percs else 0
        average_generation_mix.append({'fuel': fuel, 'perc': average})
    return average_generation_mix

def save_to_file(average_generation_mix):
    final_data = {'generationmix': average_generation_mix}
    with open('/Users/wanghaoming/Desktop/average_data.json', 'w') as f:
        json.dump(final_data, f)
    print("Average percentage for each fuel type saved to desktop.")
    return final_data

def calculate_renewable_percentage(data):
    renewable_sources = ['nuclear', 'hydro', 'solar', 'wind']
    renewable_percentage = sum(entry['perc'] for entry in data['generationmix'] if entry['fuel'] in renewable_sources)
    return renewable_percentage

def publish_mqtt_renewable_percentage(renewable_percentage):
    formatted_percentage = renewable_percentage / 100
    client = mqtt.Client(protocol=mqtt.MQTTv311)
    client.username_pw_set(mqtt_username, mqtt_password)
    client.connect(mqtt_broker, mqtt_port, 60)
    client.publish(mqtt_topic_renewable_perc, f"{formatted_percentage:.4f}")
    client.disconnect()
    print(f"Renewable Percentage: {formatted_percentage:.4f} published to MQTT.")

def get_page_content():
    options = webdriver.ChromeOptions()
    options.add_argument('--headless')  
    options.add_argument('--disable-gpu')
    options.add_argument('--no-sandbox')
    options.add_argument('--disable-dev-shm-usage')

    service = Service(driver_path)
    driver = webdriver.Chrome(service=service, options=options)
    driver.get(url)

   
    time.sleep(15)

    try:
        
        page_content = driver.page_source
    except Exception as e:
        print(f"Error getting page source: {e}")
        page_content = None

    driver.quit()
    return page_content


def extract_energy_this_month(html_content):
    soup = BeautifulSoup(html_content, 'html.parser')
    data_div = soup.find('div', {'id': 'se-this-month-energy'})  
    if data_div:
        energy_this_month_div = data_div.find('div', {'class': 'se-site-overview-data-box-data'})
        if energy_this_month_div:
            energy_this_month_text = energy_this_month_div.text.strip()
           
            energy_this_month_number = re.findall(r'\d+\.\d+', energy_this_month_text)
            if energy_this_month_number:
                energy_value = float(energy_this_month_number[0])
                
                unit = energy_this_month_text.split()[-1]  
                if unit == "MWh":
                    energy_value *= 1000  
                return energy_value
            else:
                print("Could not extract the number from the text")
                return None
        else:
            print("Could not find the data-box-data class within se-this-month-energy div")
            return None
    else:
        print("HTML content does not contain the expected id 'se-this-month-energy'")
        return None


def publish_to_mqtt(client, topic, message):
    client.publish(topic, message)


def on_message(client, userdata, message):
    global initial_electricity_value, renewable_perc_value
    if message.topic == mqtt_topic_electricity:
        initial_electricity_value = float(message.payload.decode())
    elif message.topic == mqtt_topic_renewable_perc:
        renewable_perc_value = float(message.payload.decode())

def job():
    global initial_electricity_value, renewable_perc_value, energy_this_month_value, renewables_from_grid

    
    html_content = get_page_content()

    
    if html_content:
        energy_this_month = extract_energy_this_month(html_content)
        if energy_this_month:
            energy_this_month_value = energy_this_month 
            print(f"Energy this month: {energy_this_month_value} kWh")

            if initial_electricity_value is not None:
                new_electricity_value = initial_electricity_value - energy_this_month_value
                print(f"New electricity value: {new_electricity_value:.4f} kWh")

                if renewable_perc_value is not None:
                    renewables_from_grid = new_electricity_value * renewable_perc_value
                    print(f"Renewables from grid: {renewables_from_grid:.4f} kWh")

                    
                    net_zero_ratio = (renewables_from_grid + energy_this_month_value) / initial_electricity_value
                    print(f"Net zero ratio: {net_zero_ratio:.4f}")
                    publish_to_mqtt(client, mqtt_topic_net_zero_ratio, f"{net_zero_ratio:.4f}")
                else:
                    print("Renewable percentage value is not set yet.")
            else:
                print("Initial electricity value is not set yet.")
        else:
            print("No data found")
    else:
        print("Failed to get page content")

    
    threading.Timer(60, job).start()


client = mqtt.Client()
client.username_pw_set(mqtt_username, mqtt_password)
client.on_message = on_message

client.connect(mqtt_broker, mqtt_port, 60)
client.subscribe([(mqtt_topic_electricity, 0), (mqtt_topic_renewable_perc, 0)])


client.loop_start()

def main():
    base_url = "https://api.carbonintensity.org.uk/regional/intensity"
    postcode = "WC1H"
    start_date = datetime(2024, 7, 1, tzinfo=timezone.utc)
    end_date = datetime.now(timezone.utc)

    fuel_perc = {}

    try:
        for from_date, to_date in split_date_range(start_date, end_date):
            print(f"Fetching data from {from_date} to {to_date}")
            data = fetch_data(base_url, from_date.isoformat(), to_date.isoformat(), postcode)
            if 'data' in data and isinstance(data['data'], dict) and 'data' in data['data']:
                inner_data = data['data']['data']
                print(f"Number of entries from {from_date} to {to_date}: {len(inner_data)}")
                for entry in inner_data:
                    if 'generationmix' in entry:
                        for mix in entry['generationmix']:
                            if mix['fuel'] in fuel_perc:
                                fuel_perc[mix['fuel']].append(mix['perc'])
                            else:
                                fuel_perc[mix['fuel']] = [mix['perc']]
            else:
                print(f"No valid 'data' found in response for period from {from_date} to {to_date}. Response was: {data}")
    except Exception as e:
        print(f"An error occurred: {str(e)}")

    average_generation_mix = calculate_averages(fuel_perc)
    data = save_to_file(average_generation_mix)
    renewable_percentage = calculate_renewable_percentage(data)
    publish_mqtt_renewable_percentage(renewable_percentage)

    
    job()

if __name__ == "__main__":
    main()
