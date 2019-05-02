/*
 Gruppo: 
  - Mirko Rima 793435
  - Samuele Ventura 793060
  - Luca Virgilio 794866
 
 */

/*  Documentazione metodi:

      void listenForEthernetClients()
        viene richiamato in loop e controlla se ci sono richieste in entrata da parte di client
        nel caso ci siano controlla che tipo di richieste http sono state fatte e ritorna le pagine
        web assiocate ad esse, per poi interrompere la connessione

      void printHomePage()
        stampa sul client la pgina home da cui è possibile accendere il sistema di monitoraggio

      void printStatusPage()
        stampa sul client la pagina status in cui è mostrata una tabella con i vari sensori e i valori rilevati
        la pagina si auto-ricarica ogni 5 secodni per mostrare i nuovi valori rilevati dai sensori
        dalla pagina è possibile mettere in pausa il sistema di monitoraggio

      void printStopPage()
        stampa sul client la pagina che indica lo stato di pausa (stop) del sistema di monitoraggio, dalla pagina
        è possibile riattivare il sistema di monitoraggio

      void getStatusSystem()
        salva i vari valori rilevati dai sensori e li stampa a monitor seriale

      [int][long] get...()
        metodi che ritornano i valori dei sensori

      void updateSystem()
        aggiorna i vari contatori e flag globali necessari per monitorare lo stato globale del sistema e rilevare
        valori anomali nelle rilevazioni (temperatura alta, luce bassa, ...)

      void allarm()
        accende un led e fa suonare il buzzer nel caso di valori anomali

      void clearVall()
        azzera contatori e flag globali
*/

#include <math.h>
#include <Wire.h>
#include "rgb_lcd.h"
#include <SPI.h>
#include <WiFi101.h>
#include "arduino_secrets.h"
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>

// define string error event
#define errTemp "High temperature"
#define errWifi "Low connection"
#define errIntrusion "Intrusion detected"
#define errFlame "Flame detected"
#define errLight "Low light"
#define errSound "High sound"

// define variables to catch error event
boolean flagTemp = false;
boolean flagWifi = false;
boolean flagIntrusion = false;
boolean flagFlame = false;
boolean flagLight = false;
boolean flagSound = false;

// define variables to count time for every sensor
int countTemp = 0;
int countWifi = 0;
int countIntrusion = 0;
int countFlame = 0;
int countLight = 0;
int countSound = 0;

// variabili per il sensore di temperatura con grove
// B value of the thermistor
const int B = 4275;
// R0 = 100k
const int R0 = 100000;
const int pinTempSensor = A0;

//sensore di suono
const int pinAdc = A5;

// sensore di prossimità
int triggerPort = 6;
int echoPort = 7;

//flame sensor KY-026
// definisci l'interfaccia del LED
int Led = 13 ;
// definisci sensore digitale
int buttonpin = 1;
// define la variabile numerica val
int val ;
// variabile per leggere il valore analogico
float sensor;

//boolean control = false;

// inizializzazione
long distance, light, sound, wifi;
int flame = 0;
float temperature;

// display
const int colorR = 255;
const int colorG = 0;
const int colorB = 0;
rgb_lcd lcd;

//inizializzazione WiFi
IPAddress ip(149, 132, 182, 63);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(149, 132, 2, 3);
IPAddress gateway(149, 132, 182, 1);

// inizializzazione DB MySQL
// IP del server MySQL
IPAddress server_addr(149, 132, 182 , 136);
// MySQL login user
char user[] = MYSQL_USER;
// MySQL login password
char password[] = SECRET_MYSQL_PASS;
// lunghezza query
char query[256];
// query da inviare al database
char INSERT_DATA[] = "INSERT INTO `iot01`.`sensor` (`temperature`, `distance`,`sound`, `light`,`flame`, `WiFi`) VALUES ('%0.2f', '%0.2f', '%0.2f', '%0.2f', %d, '%0.2f')";
// connessione al database
WiFiClient  clientDB;
WiFiClient  client;
MySQL_Connection conn((Client *)&clientDB);

// variabile per poter connettere client
WiFiServer server(80);

// user e password del WiFi
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
// chiave di index della rete (necessario solo per WEP)
int keyIndex = 0;
int status = WL_IDLE_STATUS;

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------

void setup() {
  // inizializza porta seriale
  Serial.begin(9600);
  WiFi.begin(ssid, pass);
  lcd.begin(16, 2);
  //impostazione dei pin
  pinMode(triggerPort, OUTPUT);
  pinMode(echoPort, INPUT);
  //flame sensor
  pinMode (Led, OUTPUT);
  pinMode (buttonpin, INPUT);
  //buzzer
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW);
  //led
  pinMode(4, OUTPUT);
  lcd.setCursor(0, 0);
  lcd.print("ASSIGNMENT 1:");
  lcd.setCursor(0, 1);
  lcd.print("HOME MONITORING");
  delay(3000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("RIMA - VENTURA");
  lcd.setCursor(2, 1);
  lcd.print("VIRGILIO");
  delay(3000);
  lcd.clear();
  lcd.setRGB(0, 255, 0);
}

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------

void loop() {
  //configurazione WiFi e tentativo di connessione
  WiFi.config(ip, dns, gateway, subnet);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("In attesa di connettersi al SSID: ");
    Serial.println(SECRET_SSID);

    while (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, pass);
      Serial.println(".");
      delay(10000);
    }
    Serial.println("\nConnesso.");
    // avvio server
    server.begin();
    Serial.println("Server Arduino avviato.");
    // sei connesso, ora stampa lo stato della connessione
    printWifiStatus();
  }
  // Tentativo di connessione al database
  if (conn.connect(server_addr, 3306, user, password)) {
    delay(1000);
  }
  else {
    Serial.println("Connessione fallita.");
  }
  // ascolto se ci sono client, nel caso attivo rilevazione sensori solo se il client lo richiede
  listenForEthernetClients();
}

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------
// FUNZIONI

// allarme: accende il led e fa suonare il buzzer
void allarm() {
  for (int i = 0; i < 2; i++)
  {
    digitalWrite(3, HIGH);
    digitalWrite(4, HIGH);
    delay(500);
    digitalWrite(3, LOW);
    digitalWrite(4, LOW);
  }
}

void listenForEthernetClients() {

  client = server.available();   // listen for incoming clients

  if (client) {                             // if you get a client,
    Serial.println("new client");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            printHomePage();
            break;
          }
          else {      // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        }
        else if (c != '\r') {    // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        // if for every request

        if (currentLine.endsWith("GET /ON")) {
          getStatusSystem();
          printStatusPage();
          break;
        }

        if (currentLine.endsWith("GET /OFF")) {
          printStopPage();
          cleanVar();
          break;
        }
      }
    }
    // close the connection:
    client.stop();
    Serial.println("client disonnected");
  }
}

void printHomePage() {

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();
  client.print("<title> System monitoring</title>");
  client.print("<button onclick=\"window.location.href = 'ON';\">Click to turn on monitoring system</button>");
  client.println();
}

void printStatusPage() {

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();
  client.println("<html>");
  client.print("<head>");

  client.print("<meta http-equiv=\"refresh\" content=\"5\">"); // per refreshare la pagine ogni content secondi, rimanda la get

  client.print("<title> System monitoring</title>");
  client.print("</head>");
  client.println("<body>");
  client.print("<style> table ,tr, td{ border:1px solid black; border-collapse:collapse;}");
  client.println("tr, td {padding:5px;}</style>");

  client.print("<button onclick=\"window.location.href = 'OFF';\">Click to stop the monitoring system</button>");



  client.println("<br>");
  client.println("<br>");
  client.print("<table>");
  client.print("<tr>");
  client.println("<td>Temperature</td>");
  client.print("<td>");

  if (!flagTemp) {
    client.print(temperature);
  }
  else {
    client.print(errTemp);
  }

  client.println("</td>");
  client.println("</tr>");
  client.print("<tr>");
  client.println("<td>WiFi Power</td>");
  client.print("<td>");

  if (!flagWifi) {
    client.print(wifi);
  }
  else {
    client.print(errWifi);
  }


  client.println("</td>");
  client.println("</tr>");
  client.print("<tr>");
  client.println("<td>Distance</td>");
  client.print("<td>");

  if (!flagIntrusion) {
    client.print(distance);
  }
  else {
    client.print(errIntrusion);
  }

  client.println("</td>");
  client.println("</tr>");
  client.print("<tr>");
  client.println("<td>Flame</td>");
  client.print("<td>");

  if (!flagFlame) {
    client.print(flame);
  }
  else {
    client.print(errFlame);
  }

  client.println("</td>");
  client.println("</tr>");
  client.print("<tr>");
  client.println("<td>Light</td>");
  client.print("<td>");

  if (!flagLight) {
    client.print(light);
  }
  else {
    client.print(errLight);
  }

  client.println("</td>");
  client.println("</tr>");
  client.print("<tr>");
  client.println("<td>Sound</td>");
  client.print("<td>");

  if (!flagSound) {
    client.print(sound);
  }
  else {
    client.print(errSound);
  }

  client.println("</td>");
  client.println("</tr>");
  client.println("</table");
  client.println("</body></html>");
  client.println();
}

void printStopPage() {

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();
  client.println("<html>");
  client.print("<head>");
  client.print("<title> System monitoring</title>");
  client.print("</head>");
  client.println("<body>");
  client.print("<style> table ,tr, td{ border:1px solid black; border-collapse:collapse;}");
  client.println("tr, td {padding:5px;}</style>");
  client.print("<button onclick=\"window.location.href = 'ON';\">Click to start the monitoring system</button>");
  client.println("<br>");
  client.println("<br>");
  client.print("<table>");
  client.print("<tr>");
  client.println("<td>Temperature</td>");
  client.print("<td>");
  client.print("stop");
  client.println("</td>");
  client.println("</tr>");
  client.print("<tr>");
  client.println("<td>WiFi Power</td>");
  client.print("<td>");
  client.print("stop");
  client.println("</td>");
  client.println("</tr>");
  client.print("<tr>");
  client.println("<td>Distance</td>");
  client.print("<td>");
  client.print("stop");
  client.println("</td>");
  client.println("</tr>");
  client.print("<tr>");
  client.println("<td>Flame</td>");
  client.print("<td>");
  client.print("stop");
  client.println("</td>");
  client.println("</tr>");
  client.print("<tr>");
  client.println("<td>Light</td>");
  client.print("<td>");
  client.print("stop");
  client.println("</td>");
  client.println("</tr>");
  client.print("<tr>");
  client.println("<td>Sound</td>");
  client.print("<td>");
  client.print("stop");
  client.println("</td>");
  client.println("</tr>");
  client.println("</table");
  client.println("</body></html>");
  client.println();
}

// Potenza WiFi
// Stampa lo status del WiFi
void printWifiStatus() {

  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  IPAddress ip = WiFi.localIP();
  Serial.print("Indirizzo IP: ");
  Serial.println(ip);

  // stampa la potenza del segnale ricevuto:
  long rssi = WiFi.RSSI();
  Serial.print("potenza del segnale (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

void getStatusSystem() {

  // SENSORE TEMPERATURA *****************************************************************************************
  temperature = getTemperature();
  // stampa a seriale
  Serial.print("temperature = ");
  Serial.println(temperature);

  // RILEVAZIONE POTENZA WiFi **************************************************************************************
  wifi = getWifiStatus();
  // stampa a seriale
  Serial.print("potenza del wifi = ");
  Serial.println(wifi);

  // SENSORE PROSSIMITA' *******************************************************************************************
  distance = getDistanza();
  // stampa a seriale
  Serial.print("distanza: ");
  Serial.println(distance);

  // SENSORE DI FLAME DETECTION **************************************************************************************
  flame = getflame();
  // stampa a seriale
  Serial.print("fiamma: ");
  Serial.println(flame);

  // SENSORE DI LUCE **************************************************************************************************
  light = getlight();
  // stampa a seriale
  Serial.print("luce: ");
  Serial.println(light);

  // SENSORE DI SUONO ************************************************************************************************
  sound = getsound();
  // stampa a seriale
  Serial.print("suono: ");
  Serial.println(sound);

  // AGGIORNO CONTATORI INTERNI PER REGISTRARE STATO SISTEMA
  updateSystem();
  // INVIO DATI AL DATABASE *****************************************************************************************
  WriteMultiToDB(temperature, distance, sound, light, flame, wifi);



}

float getTemperature() {

  int a = analogRead(pinTempSensor);
  float R = 1023.0 / a - 1.0;
  R = R0 * R;
  temperature = 1.0 / (log(R / R0) / B + 1 / 298.15) - 273.15;

  return temperature;

}

// Status del WiFi
long getWifiStatus() {
  // prendi la forza del segnale ricevuto
  long rssi = WiFi.RSSI();
  return rssi;
}

// rileva la distanza: sensore di prossimità HC-SR04
long getDistanza()
{
  //porta bassa l'uscita del trigger
  digitalWrite( triggerPort, LOW);
  delayMicroseconds(2);
  //invia un impulso di 10microsec su trigger
  digitalWrite(triggerPort, HIGH);
  delayMicroseconds(10);
  digitalWrite(triggerPort, LOW);
  // prende in ingresso il segnale nella porta echo e tramite un calcolo trova distanza (r)
  long duration = pulseIn( echoPort, HIGH);
  long r = 0.034 * duration / 2;
  delay(100);
  return r;
}

// rileva la luce
long getlight()
{
  int light = analogRead(A3);
  // mappa i valori della luce in un intervallo da 0 a 10
  light = map(light, 0, 800, 0, 10);
  delay(100);
  return light;
}

// rileva il suono
long getsound()
{
  long sum = 0;
  for (int i = 0; i < 32; i++)
  {
    sum += analogRead(pinAdc);
  }

  sum >>= 5;
  delay(10);
  return sum;

}

// rileva se ci sono fiamme
int getflame()
{
  val = digitalRead (buttonpin) ;
  // quando rileva la fiamma accende il led verde del sensore
  if (val == HIGH)
  {
    digitalWrite (Led, HIGH);
  }
  else
  {
    digitalWrite (Led, LOW);
  }
  return val;
}


// esegui la query al database
int WriteMultiToDB(float field1, float field2, float field3, float field4, int field5, float field6) {

  MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);

  sprintf(query, INSERT_DATA, field1, field2, field3, field4, field5, field6);
  Serial.println(query);
  // Esegui la query
  cur_mem->execute(query);
  delete cur_mem;

}

void updateSystem() {

  // temperatura
  if (temperature > 30 and countTemp < 2) {
    countTemp ++;
    flagTemp = false;
  } else if (temperature > 30 and countTemp >= 2) {
    allarm();
    // stampa a monitor
    printOnLcd(0, 0, errTemp, 1000, true);
    // set flag for catch event
    flagTemp = true;
  } else if (temperature <= 30 and countTemp > 0) {
    countTemp --;
    flagTemp = false;
  }


  // wifi
  if (wifi > - 25 and countWifi < 2) {
    countWifi ++;
    flagWifi = false;
  } else if (wifi > - 25 and countWifi <= 2) {
    allarm();
    // stampa a monitor
    printOnLcd(0, 0, errWifi, 1000, true);
    flagWifi = true;
  } else if (wifi <= - 25 and countWifi > 0) {
    countWifi --;
    flagWifi = false;
  }

  // distance
  if (distance < 10 and countIntrusion < 2) {
    countIntrusion ++;
    flagIntrusion = false;
  } else if (distance < 10 and countIntrusion >= 2) {
    allarm();
    // stampa a monitor
    printOnLcd(0, 0, errIntrusion, 1000, true);
    flagIntrusion = true;
  } else if (distance >= 10 and countIntrusion > 0) {
    countIntrusion --;
    flagIntrusion = false;
  }

  // flame
  if (flame == 1 and countFlame < 2) {
    countFlame ++;
    flagFlame = false;
  } else if (flame == 1 and countFlame >= 2) {
    allarm();
    // stampa a monitor
    printOnLcd(0, 0, errFlame, 1000, true);
    flagFlame = true;
  } else if (flame == 0 and countFlame > 0) {
    countFlame --;
    flagFlame = false;
  }

  // light
  if (light < 4 and countLight < 2) {
    countLight ++;
    flagLight = false;
  } else if (light < 4 and countLight >= 2) {
    allarm();
    // stampa a monitor
    printOnLcd(0, 0, errLight, 1000, true);
    flagLight = true;
  } else if (light >= 4 and countLight > 0) {
    countLight --;
    flagLight = false;
  }

  // sound
  if (sound > 40 and countSound < 2) {
    countSound ++;
    flagSound = false;
  } else if (sound > 40 and countSound >= 2) {
    allarm();
    // stampa a monitor
    printOnLcd(0, 0, errSound, 1000, true);
    flagSound = true;
  } else if (sound <= 40 and countSound > 0) {
    countSound --;
    flagSound = false;
  }
}

void cleanVar() {

  // flag event
  flagTemp = false;
  flagWifi = false;
  flagIntrusion = false;
  flagFlame = false;
  flagLight = false;
  flagSound = false;

  // count
  countTemp = 0;
  countWifi = 0;
  countIntrusion = 0;
  countFlame = 0;
  countLight = 0;
  countSound = 0;
}

void printOnLcd(int start, int finish, String messaggio, int del, boolean c) {

  lcd.setCursor(start, finish);
  lcd.setRGB(colorR, colorG, colorB);
  lcd.print(messaggio);
  delay(del);
  if (c)
    lcd.clear();
  lcd.setRGB(0, 255, 0);
}
