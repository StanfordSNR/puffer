#!/usr/bin/env python3

'''
This script runs a headless Firefox browser in the background and generates
a publicly accessible Grafana snapshot with a lifetime of 1 hour.
It also stores the URL of this driver in the postgres database, so
Django can access it and redirect users to the URL of the most recent snap
'''

import os
from datetime import datetime

import psycopg2
import selenium
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import Select
from selenium.webdriver.firefox.options import Options


INFLUX_PWD = os.getenv("INFLUXDB_PASSWORD")
GRAFANA_PWD = os.getenv("GRAFANA_PASSWORD")


def main():
    options = Options()
    options.set_headless(headless=True)
    driver = webdriver.Firefox(firefox_options=options)
    driver.implicitly_wait(30)
    driver.get("https://puffer.stanford.edu/grafana/login")
    driver.find_element_by_name("username").click()
    driver.find_element_by_name("username").clear()
    driver.find_element_by_name("username").send_keys("admin")
    driver.find_element_by_id("inputPassword").click()
    driver.find_element_by_id("inputPassword").clear()
    driver.find_element_by_id("inputPassword").send_keys(GRAFANA_PWD)
    xpath = ("(.//*[normalize-space(text()) and normalize-space(.)"
             "='Help'])[1]/following::button[1]")
    driver.find_element_by_xpath(xpath).click()
    xpath = ("(.//*[normalize-space(text()) and normalize-space(.)"
             "='Help'])[1]/following::i[5]")
    driver.find_element_by_xpath(xpath).click()
    driver.find_element_by_link_text("Snapshot").click()
    xpath = ("(.//*[normalize-space(text()) and normalize-space(.)"
             "='Expire'])[1]/following::select[1]")
    driver.find_element_by_xpath(xpath).click()
    xpath = ("(.//*[normalize-space(text()) and normalize-space(.)"
             "='Expire'])[1]/following::select[1]")
    Select(driver.find_element_by_xpath(xpath)).select_by_visible_text("1 Hour")
    xpath = ("(.//*[normalize-space(text()) and normalize-space(.)"
             "='Expire'])[1]/following::select[1]")
    driver.find_element_by_xpath(xpath).click()
    xpath = ("(.//*[normalize-space(text()) and normalize-space(.)"
             "='Timeout (seconds)'])[1]/following::button[1]")
    driver.find_element_by_xpath(xpath).click()
    prefix = "https://puffer.stanford.edu/grafana/dashboard/snapshot/"
    snapshot_url = driver.find_element_by_partial_link_text(prefix).text
    driver.quit()

    # Now, add this link to postgres, and delete old links from the table
    time = datetime.utcnow()
    conn = psycopg2.connect(database="puffer", user="puffer", password=INFLUX_PWD,
                            host="puffer-dev.c78hjwwa209d.us-west-1.rds.amazonaws.com",
                            port="5432", sslmode="require")
    cur = conn.cursor()
    add_snap_cmd = ("INSERT INTO puffer_grafanasnapshot "
                    "(url, created_on) VALUES (%s, %s)")
    cur.execute(add_snap_cmd, (snapshot_url, time))
    conn.commit()
    del_old_snap_cmd = "DELETE FROM puffer_grafanasnapshot WHERE (url) != (%s)"
    cur.execute(del_old_snap_cmd, (snapshot_url,))
    conn.commit()
    cur.close()


if __name__ == '__main__':
    main()
