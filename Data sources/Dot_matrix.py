import time
import threading
from selenium import webdriver
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.common.by import By
from bs4 import BeautifulSoup
import re
import requests
import json
from paho.mqtt import client as mqtt_client


interval = 600


broker = 'mqtt.cetools.org'
port = 1884
topic_dot_matrix = 'student/CASA0022/zczqhw8/Dot_matrix'
username = 'student'
password = 'ce2021-mqtt-forget-whale'


driver_path = '/Users/wanghaoming/Downloads/chromedriver-mac-arm64/chromedriver'
url = 'https://monitoringpublic.solaredge.com/solaredge-web/p/kiosk?guid=2dd7fbae-b3f0-417c-b67b-e2759946a622'

def connect_mqtt():
    client = mqtt_client.Client(client_id='', clean_session=True)
    client.username_pw_set(username, password)
    client.connect(broker, port)
    return client

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

def extract_energy_today(html_content):
    soup = BeautifulSoup(html_content, 'html.parser')
    data_div = soup.find('div', {'id': 'se-today-energy'})
    if data_div:
        energy_today_div = data_div.find('div', {'class': 'se-site-overview-data-box-data'})
        if energy_today_div:
            energy_today_text = energy_today_div.text.strip()
            energy_today_number = re.findall(r'\d+\.\d+', energy_today_text)
            if energy_today_number:
                return float(energy_today_number[0])
            else:
                print("Could not extract the number from the text")
                return None
        else:
            print("Could not find the data-box-data class within se-today-energy div")
            return None
    else:
        print("HTML content does not contain the expected id 'se-today-energy'")
        return None

def get_carbon_intensity_forecast(postcode='WC1H'):
    url = f'https://api.carbonintensity.org.uk/regional/postcode/{postcode}'
    response = requests.get(url, headers={'Accept': 'application/json'})
    if response.status_code == 200:
        data = response.json()
        forecast = data['data'][0]['data'][0]['intensity']['forecast']
        return float(forecast)
    else:
        print("Failed to retrieve data:", response.status_code)
        return None

def publish(client, topic, message):
    result = client.publish(topic, message)
    status = result[0]
    if status == 0:
        print(f"Sent data to topic `{topic}`")
    else:
        print(f"Failed to send message to topic `{topic}`")

def job():
    client = connect_mqtt()
    html_content = get_page_content()
    if html_content:
        energy_today = extract_energy_today(html_content)
        if energy_today:
            forecast = get_carbon_intensity_forecast()
            if forecast is not None:
                save_carbon_emissions_today = (energy_today * forecast) / 1000
                dot_matrix_value = round(save_carbon_emissions_today / 5.13, 2)
                print(f"Dot matrix value: {dot_matrix_value}")
                publish(client, topic_dot_matrix, str(dot_matrix_value))
            else:
                print("Failed to get carbon intensity forecast")
        else:
            print("No energy data found")
    else:
        print("Failed to get page content")

    threading.Timer(interval, job).start()
    client.loop_forever()

if __name__ == '__main__':
    job()
