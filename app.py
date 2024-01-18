import json
import csv

from flask import request
from flask import jsonify
from flask import Flask
from flask import session
from flask import render_template
#https://python-adv-web-apps.readthedocs.io/en/latest/flask.html

#https://www.emqx.com/en/blog/how-to-use-mqtt-in-flask
from flask_mqtt import Mqtt
from flask_pymongo import PyMongo
from pymongo import MongoClient

#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
# Initialisation :  Mongo DataBase

# Connect to Cluster Mongo : attention aux permissions "network"/MONGO  !!!!!!!!!!!!!!!!
ADMIN=False # Faut etre ADMIN pour ecrire dans la base
#client = MongoClient("mongodb+srv://menez:monpassadminQ@cluster0.x0zyf.mongodb.net/?retryWrites=true&w=majority")
client = MongoClient("mongodb+srv://visitor:doliprane@cluster0.seoqsai.mongodb.net/?retryWrites=true&w=majority")

# db is an attribute of client =>  all databases 

# Looking for "WaterBnB" database
#https://stackoverflow.com/questions/32438661/check-database-exists-in-mongodb-using-pymongo
dbname= 'WaterBnB'
dbnames = client.list_database_names()
if dbname in dbnames: 
    print(f"{dbname} is there!")
else:
    print("YOU HAVE to CREATE the db !\n")
    
db = client.WaterBnB

# Looking for "users" collection in the WaterBnB database
collname= 'users'
collnames = db.list_collection_names()
if collname in collnames: 
    print(f"{collname} is there!")
else:
    print(f"YOU HAVE to CREATE the {collname} collection !\n")
    
userscollection = db.users
poolscollection = db.pools
#-----------------------------------------------------------------------------
# import authorized users .. if not already in ?
if ADMIN :
    userscollection.delete_many({})  # empty collection
    excel = csv.reader(open("usersM1_2023.csv")) # list of authorized users
    for l in excel : #import in mongodb
        ls = (l[0].split(';'))
        #print(ls)
        if userscollection.find_one({"name" : ls[0]}) ==  None:
            userscollection.insert_one({"name": ls[0], "num": ls[1]})
    
#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
# Initialisation :  Flask service
app = Flask(__name__)

# Notion de session ! .. to share between routes !
# https://flask-session.readthedocs.io/en/latest/quickstart.html
# https://testdriven.io/blog/flask-sessions/
# https://www.fullstackpython.com/flask-globals-session-examples.html
# https://stackoverflow.com/questions/49664010/using-variables-across-flask-routes
app.secret_key = 'BAD_SECRET_KEY'

#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%        
# Initialisation MQTT
app.config['MQTT_BROKER_URL'] =  "test.mosquitto.org"
app.config['MQTT_BROKER_PORT'] = 1883
#app.config['MQTT_USERNAME'] = ''  # Set this item when you need to verify username and password
#app.config['MQTT_PASSWORD'] = ''  # Set this item when you need to verify username and password
#app.config['MQTT_KEEPALIVE'] = 5  # Set KeepAlive time in seconds
app.config['MQTT_TLS_ENABLED'] = False  # If your broker supports TLS, set it True

topicname = "uca/iot/piscine"
topicname_second = "uca/iot/tajine_granted"
mqtt_client = Mqtt(app)


  
#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%        
@app.route('/')
def hello_world():
    return render_template('index.html') #'Hello, World!'

#Test with =>  curl https://waterbnbf.onrender.com/

#%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%        
"""
#https://stackabuse.com/how-to-get-users-ip-address-using-flask/
@app.route("/ask_for_access", methods=["POST"])
def get_my_ip():
    ip_addr = request.remote_addr
    return jsonify({'ip asking ': ip_addr}), 200

# Test/Compare with  =>curl  https://httpbin.org/ip

#Proxies can make this a little tricky, make sure to check out ProxyFix
#(Flask docs) if you are using one.
#Take a look at request.environ in your particular environment :
@app.route("/ask_for_access", methods=["POST"])
def client():
    ip_addr = request.environ['REMOTE_ADDR']
    return '<h1> Your IP address is:' + ip_addr
"""

#https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/X-Forwarded-For
#If a request goes through multiple proxies, the IP addresses of each successive proxy is listed.
# voir aussi le parsing !

@app.route("/open", methods=['GET', 'POST'])
def openthedoor():
    granted = "NO"

    idu = request.args.get('idu')
    idswp = request.args.get('idswp')

    if idu and idswp:
        session['idu'] = idu
        session['idswp'] = idswp

        ip_addr = request.environ.get('HTTP_X_FORWARDED_FOR', request.remote_addr)

        user_exists = userscollection.find_one({"name": idu}) is not None
        pool = poolscollection.find_one_and_update(
            {"pool_id": idswp, "occuped": False},
            {"$set": {"granted": "YES"}},
            return_document=True
        ) if idswp else None

        print(f'user exists : {user_exists}')
        print(f'pool exists : {pool}')

        if user_exists and pool:
            print("granted")
            granted = "YES"


        print("id pool request", idswp)
        print("Condition realized to publish message :", idswp == "P_22311680" or idswp == "P_22312300")
        if idswp == "P_22311680" or idswp == "P_22312300": 
            mqtt_client.publish(topicname_second, granted)

    return jsonify({'idu': session.get('idu', ''), 'idswp': session.get('idswp', ''), "granted": granted}), 200
    

# Test with => curl -X POST https://waterbnbf.onrender.com/open?who=gillou
# Test with => curl https://waterbnbf.onrender.com/open?who=gillou

@app.route('/testmqtt')
def test_mqtt():
    try:
        publish_result = mqtt_client.publish(topicname_second, "Test Message")
        return jsonify({'result': 'Message published', 'details': str(publish_result)})
    except Exception as e:
        return jsonify({'error': str(e)})
    

@app.route("/users")
def lists_users(): # Liste des utilisateurs déclarés
    todos = userscollection.find()
    return jsonify([todo['name'] for todo in todos])

@app.route('/publish', methods=['POST'])
def publish_message():
   request_data = request.get_json()
   publish_result = mqtt_client.publish(request_data['topic'], request_data['msg'])
   return jsonify({'code': publish_result[0]})

@mqtt_client.on_connect()
def handle_connect(client, userdata, flags, rc):
   if rc == 0:
       print('Connected successfully')
       mqtt_client.subscribe(topicname) # subscribe topic
       mqtt_client.subscribe(topicname_second) # subscribe topic
   else:
       print('Bad connection. Code:', rc)


@mqtt_client.on_message()
def handle_mqtt_message(client, userdata, msg):
    if msg.topic == topicname:
        decoded_message = str(msg.payload.decode("utf-8"))

        try:
            dic = json.loads(decoded_message)

            # Extract required information from the message
            who = dic.get("info", {}).get("ident")
            t = dic.get("status", {}).get("temperature")
            hotspot = dic.get("piscine", {}).get("hotspot")
            occuped = dic.get("piscine", {}).get("occuped")

            # Ensure all required fields are present
            if who is not None and t is not None and hotspot is not None and occuped is not None:
                piscine = {
                    "pool_id": who,
                    "temp": t,
                    "hotspot": hotspot,
                    "granted" : "YES",
                    "occuped": occuped
                }

                # Update the global variable with the new data
                existing_pool = poolscollection.find_one({"pool_id": who})

                if existing_pool is None:
                    # If the pool doesn't exist, insert it
                    poolscollection.insert_one(piscine)
                else:
                    # If the pool exists, update it
                    poolscollection.update_one({"pool_id": who}, {"$set": piscine})

            else:
                print(f"Incomplete data in the received message from {who}")

        except json.JSONDecodeError as e:
            print(f"JSONDecodeError: Failed to decode the received message - {e}")
        except Exception as e:
            print(f"An error occurred: {e}")
    if msg.topic == topicname_second:
        print("message received on topic_granted topic !")

        


#%%%%%%%%%%%%%  main driver function
if __name__ == '__main__':

    # run() method of Flask class runs the application 
    # on the local development server.
    app.run(debug=False, use_reloader=False) #host='127.0.0.1', port=5000)
    