#!/usr/bin/env python
from flask import Flask, render_template, request, jsonify
from datetime import datetime
import json
import subprocess
import time
import threading
import os

CONFIGFILE="../bin/config.json"
pypath=__file__
print(pypath)
TASKPATH=pypath.replace('/frontend/app.py', '/bin')
TASK=pypath.replace('/frontend/app.py', '/bin/start.sh')
print(TASK)
app = Flask(__name__)

def write_config(data):
    global p
    old=load_config()
    print("old :" , old)
    
    old["username"]=data["username"]
    old["password"]=data["password"]

    if(data["host"]!=''):
        old["host"]=data["host"]

    if(data["port"]!=''):
        old["port"]=data["port"]

    if(data["clientid"]!=""):
        old["clientid"]=data["clientid"]

    if(data["interval"]!=""):
        old["interval"]=data["interval"]

    if(data["luxoffset"]==""):
        old["luxoffset"]="0"
    else:
        old["luxoffset"]=data["luxoffset"]

    if(data["tempoffset"]==""):
        old["tempoffset"]="0"
    else:
        old["tempoffset"]=data["tempoffset"]

    if(data["humoffset"]==""):
        old["humoffset"]="0"
    else:
        old["humoffset"]=data["humoffset"]

    if(data["pm25offset"]==""):
        old["pm25offset"]="0"
    else:
        old["pm25offset"]=data["pm25offset"]
        
    print("new :" , old)

    with open(CONFIGFILE, 'w') as json_file:
        json.dump(old, json_file)

        p.kill()
        returncode = p.wait()
        print ("Returncode: ", returncode)
        
        killer = subprocess.Popen(['ps', '-A'], stdout=subprocess.PIPE)
        output, error = killer.communicate()
        
        target_process = "Telemetry"
        for line in output.splitlines():
            if target_process in str(line):
                print(line)
                pid = int(line.split(None, 1)[0])
                os.kill(pid, 9)
        
        p = subprocess.Popen(TASK, cwd=TASKPATH)
        print ("Restarted with PID :", p.pid)


def load_config():
    f = open(CONFIGFILE)
    data = json.load(f)
    j=0
    for i in data:
        j=j+1
    f.close()
    if ( j==10):
        return data

@app.route("/")
def hello_there():
    config=load_config()
    if (config):
        return render_template(
            "index.html",
            VERSION=1.0,
            USERNAME=config["username"],
            PASSWORD=config["password"],
            HOST=config["host"],
            PORT=config["port"],
            #pubtopic,
            #subtopic,
            DEVID=config["clientid"],
            INTERVAL=config["interval"],
            LUXOFFSET=config["luxoffset"],
            TEMPOFFSET=config["tempoffset"],
            HUMOFFSET=config["humoffset"],
            PM25OFFSET=config["pm25offset"]
        )
    else:
        return render_template(
            "index.html")

@app.route("/config")
def get_data():
    return json.dumps(load_config())

@app.route("/settings",  methods = ['POST'])
def hello():
    js=request.get_json()
    write_config(js)
    return json.dumps({'success':True}), 200, {'ContentType':'application/json'} 
        
if __name__ == '__main__':
    p = subprocess.Popen(TASK, cwd=TASKPATH)
    print ("PID of telemetry task :", p.pid)
    app.run(host="0.0.0.0", port=8080)
    


