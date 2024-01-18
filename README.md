# WaterBnBF_22311680

Membres de l'équipe : Amine HADDOU et Marouan BOULLI

**Serveur render** : https://waterbnb-22311680.onrender.com/

**Graphiques MongoDB** : https://charts.mongodb.com/charts-project-0-asysu/public/dashboards/c11acb46-f078-499e-83e7-5e8c6a2bba0f

## Communication Flask -> ESP32

#### MQTT
Nous avons dans un premier temps essayé d'envoyer la valeur de "granted" en créeant un nouveau topic mqtt.  
L'idée est que le serveur Flask publie cette valeur sur le topic auquel l'ESP est également inscrit.  
L'ESP récupère la valeur en écoutant les messages parvenant sur ce topic (un message est envoyé à chaque demande d'accès pour la piscine correspondant à l'ESP) et peut ainsi attribuer ou non la couleur rouge à la LED strip.

Seulement, après plusieurs heures de debug ^^', nous ne sommes pas parvenus à publier des messages sur le topic via le serveur Flask.  
En publiant en ligne de commande on reçoit bien les messages sur l'ESP, mais pas en utilisant la publication du serveur Flask.


#### HTTP
L'autre solution que nous avons implémentée et qui fonctionne consiste à créer une nouvelle route sur le serveur Flask ```@app.route("/get_access_status/<esp_id>")```.  
Cette route retourne la valeur de "granted" stockée en base de données lors de la demande d'accès.

L'ESP exécute régulièrement des requêtes HTTP GET sur cette route pour récupérer la valeur de "granted" correspondant à l'ID de la piscine de l'ESP.



