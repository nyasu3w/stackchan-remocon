/* do not mind type name confusing */

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <DNSServer.h>
#include <BluetoothSerial.h>

#include <M5Unified.h>
#include <Avatar.h>
#include <faces/DogFace.h>

#include <SD.h>

char stackchan_id[5];

// for WiFi & Web server
constexpr unsigned int wifiap_len=32;
char ssidbase[wifiap_len];
const char *def_ssidbase= "yourAP-";
const char *def_password = "yourPassword";

char ssid[wifiap_len];
char password[wifiap_len];

WiFiServer server(80);
DNSServer dnsServer;  // for Captive portal

constexpr const char *stackchan_url = "stackchan.home.arpa";

// for BluetoothSerial
BluetoothSerial BtSerial;


// for M5Stack_avatar
using namespace m5avatar;
Avatar avatar;
Face* faces[2];
unsigned int selected_face=0;

unsigned long time_rotating=0;
int rotating_ccw=0;
unsigned long time_scaling=0;

// for servo control
#include <ServoEasing.hpp> 

int servo_pin_x = 16;
int servo_pin_y = 17;

#define START_DEGREE_VALUE_X 90
#define START_DEGREE_VALUE_Y 90

ServoEasing servo_x;
ServoEasing servo_y;

uint32_t last_mouth_millis = 0;
unsigned long speak_finish_time=0;

// params
#include <Preferences.h>
Preferences preference;

int servo_offset_x = 0;  // X軸サーボのオフセット（90°からの+-で設定）
int servo_offset_y = 0;  // Y軸サーボのオフセット（90°からの+-で設定）

int servo_x_min=45;
int servo_x_max=135;
int servo_y_min=60;
int servo_y_max=90;

bool servo_enabled=false;
int  wifi_enabled=1;

int easedelay_base=3;     // +300ms
int easedelay_factor=10;  // +(0-this)*100 ms

unsigned int move_freq=100;
int speak_speed = 15;

int primary_color=TFT_WHITE;
int background_color=TFT_BLACK;

int sd_enabled = 0;

////  preference block name
constexpr const char* pref_stackchan="stackchan";
constexpr const char* pref_wifi="wifiap";

/************************************************/
// ==== Servo functions === 
void setup_servos() {
  if (servo_x.attach(servo_pin_x, 
                     START_DEGREE_VALUE_X + servo_offset_x,
                     DEFAULT_MICROSECONDS_FOR_0_DEGREE,
                     DEFAULT_MICROSECONDS_FOR_180_DEGREE)) {
    Serial.print("Error attaching servo x");
  }
  if (servo_y.attach(servo_pin_y,
                     START_DEGREE_VALUE_Y + servo_offset_y,
                     DEFAULT_MICROSECONDS_FOR_0_DEGREE,
                     DEFAULT_MICROSECONDS_FOR_180_DEGREE)) {
    Serial.print("Error attaching servo y");
  }
  servo_x.setEasingType(EASE_QUADRATIC_IN_OUT);
  servo_y.setEasingType(EASE_QUADRATIC_IN_OUT);
  setSpeedForAllServos(60);
}

void moveX(int x, uint32_t millis_for_move = 0) {
  if (millis_for_move == 0) {
    servo_x.easeTo(x + servo_offset_x);
  } else {
    servo_x.easeToD(x + servo_offset_x, millis_for_move);
  }
}

void moveY(int y, uint32_t millis_for_move = 0) {
  if (millis_for_move == 0) {
    servo_y.easeTo(y + servo_offset_y);
  } else {
    servo_y.easeToD(y + servo_offset_y, millis_for_move);
  }
}

void moveXY(int x, int y, uint32_t millis_for_move = 0) {
  updateAndWaitForAllServosToStop();
  if (millis_for_move == 0) {
    servo_x.setEaseTo(x + servo_offset_x);
    servo_y.setEaseTo(y + servo_offset_y);
  } else {
    servo_x.setEaseToD(x + servo_offset_x, millis_for_move);
    servo_y.setEaseToD(y + servo_offset_y, millis_for_move);
  }
  //サーボの更新は関数の最初に移動してここでブロックはしない
}

void moveRandom() {
  // ランダムモード
  int x = random(servo_x_min, servo_x_max);
  int y = random(servo_y_min, servo_y_max);

  int delay_time = random(easedelay_factor) + easedelay_base;
  moveXY(x, y, 1000 + 100 * delay_time);
}

/************************************************/
// ===== stackchan control =====

size_t url_decode(char *dest, const char *text, const size_t destlen){
  // decoding url encoded text
  int pos=0;
  while(*text && pos < destlen-1){
    dest[pos]=*text;
    if(*text=='+'){
      dest[pos]=' ';
    } else if(*text=='%'){
      char code[3];
      strncpy(code,text+1,2);
      dest[pos]=(char)strtol(code,NULL,16);
      text+=2;
    }
    text++;
    pos++;
  }
  dest[pos]=0; 
  return pos;
}

void baloon_speak(const char* text) {
  Serial.printf("Speaking:<%s>\n",text);
  int len = strlen(text);
  if(len>0){
    speak_finish_time=millis() + len*speak_speed * 10 + 500;
  } else {
    speak_finish_time=0;
  }
  avatar.setSpeechText(text);
}

void set_expression(const unsigned int ex){
  static const Expression expressions[] = {
    Expression::Neutral,
    Expression::Happy,
    Expression::Sleepy,
    Expression::Doubt,
    Expression::Angry,
    Expression::Sad
  };
  if(ex < sizeof(expressions)/sizeof(Expression)) avatar.setExpression(expressions[ex]);
}

void loop_rotating(){
  if(time_rotating>0){ // face rotation
    constexpr float rspeed=0.5;  // rot/sec
    float rotation=0;
    unsigned long tpassed = millis()-time_rotating;
    if(tpassed>1000/rspeed){
      Serial.println("Rotate anim stop");
      time_rotating=0;
      rotation=0;
    }  else {
//                  // |--------  from 0 to 1000  -------------| - convert to 0-2PI -|
//      radian = (tpassed % (int)(1000.0/rspeed))*rspeed / 1000*2*3.14; // 1 rot/sec
                  // |--------  from 0 to 1000  -------------| - convert to 0-360 -|
      rotation = (tpassed % (int)(1000.0/rspeed))*rspeed / 1000.0*360;
      if(rotating_ccw) rotation=360.0-rotation;
    }
//    Serial.printf("Rotate(%f)\n",rotation);
    avatar.setRotation(rotation);
  }
}
void loop_scaling(){
  if(time_scaling>0){ // face scaling
    constexpr float sspeed=0.5;  // cycle/sec
    constexpr float initrad=3.14/3;
    float radian=0;
    unsigned long tpassed = millis()-time_scaling;
    if(tpassed>1000/sspeed){
      Serial.println("Scale anim stop");
      time_scaling=0;
      radian=initrad;  // initial rotate
    }  else {
                  // |--------  from 0 to 1000  -------| - convert to 0-2PI -|
      radian = (tpassed % (int)(1000.0/sspeed))*sspeed / 1000*2*3.14; // 1 rot/sec
      radian = initrad - radian;  // initial rotate -rad
    }
    avatar.setScale(cos(radian)*2);   // the initial erotaion is for this "2"
  }
}

void loop_avatar(){
  static long next_move_time = 0;
  long now=millis();
  bool stopped = updateAllServos();
  if( next_move_time < now && servo_enabled && stopped){  // move servo
    moveRandom();  
    next_move_time = millis() + 100 + random(0,move_freq)*100;  // not use variable "now"
  } else {  // ls speaking or shut up
    if(speak_finish_time>0){
      if(speak_finish_time>now){
        static int skip_count=0;
        skip_count++;
        if(skip_count>speak_speed/10){
          avatar.setMouthOpenRatio(random(10)/10.0);
          skip_count=0;
        } 
      } else {
        baloon_speak("");
        avatar.setMouthOpenRatio(0);
        speak_finish_time=0;
      }
    }
  }

  loop_rotating();
  loop_scaling();
}


//////

/************************************************/
// ==== param settings and request accepting === 

void store_params(){
  preference.begin(pref_stackchan, false);

  preference.putInt("servo_offset_x",servo_offset_x);
  preference.putInt("servo_offset_y",servo_offset_y);
  preference.putInt("servo_x_min",servo_x_min);
  preference.putInt("servo_x_max",servo_x_max);
  preference.putInt("servo_y_min",servo_y_min);
  preference.putInt("servo_y_max",servo_y_max);
  preference.putInt("easedelay_b",easedelay_base);
  preference.putInt("easedelay_f",easedelay_factor);
  preference.putInt("move_freq",move_freq);
  preference.putInt("speak_speed",speak_speed);
  preference.putInt("servo_pin_x",servo_pin_x);
  preference.putInt("servo_pin_y",servo_pin_y);
  preference.putInt("sel_face",selected_face);
  preference.putInt("sd_enable",sd_enabled);
  preference.putInt("wf_enable",wifi_enabled);

  ColorPalette cl=avatar.getColorPalette();
  preference.putInt("primary_color",primary_color);
  preference.putInt("bg_color",background_color);
  preference.end();
}
void load_params(){
  preference.begin(pref_stackchan, true);
  servo_offset_x = preference.getInt("servo_offset_x",servo_offset_x);
  servo_offset_y = preference.getInt("servo_offset_y",servo_offset_y);
  servo_x_min    = preference.getInt("servo_x_min",servo_x_min);
  servo_x_max    = preference.getInt("servo_x_max",servo_x_max);
  servo_y_min    = preference.getInt("servo_y_min",servo_y_min);
  servo_y_max    = preference.getInt("servo_y_max",servo_y_max);
  easedelay_base = preference.getInt("easedelay_b",easedelay_base);  // long key rejected? 
  easedelay_factor = preference.getInt("easedelay_f",easedelay_factor);
  move_freq      = preference.getInt("move_freq", move_freq);
  speak_speed    = preference.getInt("speak_speed",speak_speed);
  servo_pin_x    = preference.getInt("servo_pin_x",servo_pin_x);
  servo_pin_y    = preference.getInt("servo_pin_y",servo_pin_y);
  selected_face  = preference.getInt("sel_face",selected_face);
  sd_enabled     = preference.getInt("sd_enable",sd_enabled);
  wifi_enabled   = preference.getInt("wf_enable",wifi_enabled);

  ColorPalette cl=avatar.getColorPalette();
  cl.set(COLOR_PRIMARY, (primary_color = preference.getInt("primary_color",TFT_WHITE)));
  cl.set(COLOR_BACKGROUND, (background_color = preference.getInt("bg_color",TFT_BLACK)));
  avatar.setColorPalette(cl);

  preference.end();

  preference.begin(pref_wifi, true);
  if(preference.isKey("ssid")) {
    preference.getString("ssid",ssidbase,wifiap_len);
  } else {
    strncpy(ssidbase,def_ssidbase,wifiap_len);
  }
  if(preference.isKey("passwd")) {
    preference.getString("passwd",password,wifiap_len);
  } else {
    strncpy(password,def_password,wifiap_len);
  }
  preference.end();
}

void store_wifiap_params() {
  preference.begin(pref_wifi, false);
  preference.putString("ssid",ssidbase);
  preference.putString("passwd",password);
  preference.end();
  Serial.printf("Store wifi params: ssid=[%s], pass=[%s]\n",ssidbase,password);
}

void clear_params(){
  preference.begin(pref_stackchan, false);
  preference.clear();
  preference.end();
}
void clear_wifiparams(){
  preference.begin(pref_wifi, false);
  preference.clear();
  preference.end();
}

void set_param(String& cmd, int param) {
    Serial.printf("SetParam %s=%d\n",cmd.c_str(),param);
    if(cmd == "xo") {
      if(-10<=param && param<= 10) servo_offset_x = param;
    } else if(cmd == "yo") {
      if(-10<=param && param<= 10) servo_offset_y = param;
    } else if(cmd == "xl") {
      if(0<=param && param<= 180) servo_x_min = param;
    } else if(cmd == "xu") {
      if(0<=param && param<= 180) servo_x_max = param;
    } else if(cmd == "yl") {
      if(0<=param && param<= 180) servo_y_min = param;
    } else if(cmd == "yu") {
      if(0<=param && param<= 180) servo_y_max = param;
    } else if(cmd == "eb") {
      if(5<=param && param<= 15) easedelay_base = param;
    } else if(cmd == "ef") {
      if(0<=param && param<= 20) easedelay_factor = param;
    } else if(cmd == "fq") {
      if(10<=param && param<= 30000) move_freq = param;
    } else if(cmd == "ss") {
      if(10<=param && param<= 500) speak_speed = param;
    } else if(cmd == "px") {
      if(0<=param && param<= 99) servo_pin_x = param;
    } else if(cmd == "py") {
      if(0<=param && param<= 99) servo_pin_y = param;
    } else if(cmd == "fa") {
      if(0<=param && param<= 1) {
        selected_face = param;
        avatar.setFace(faces[selected_face]);
      }
    } else if(cmd == "sd") {
      sd_enabled = param;
    } else if(cmd == "wf") {
      wifi_enabled = param;
    }
}

void set_color(const String& req){
  Serial.println("Set color request:"+req);

  String cp=req.substring(0,req.indexOf(','));
  String cb=req.substring(req.indexOf(',')+1);

  primary_color = strtol(cp.c_str(),NULL,16);
  background_color = strtol(cb.c_str(),NULL,16);

  ColorPalette cl=avatar.getColorPalette();
  cl.set(COLOR_PRIMARY, primary_color);
  cl.set(COLOR_BACKGROUND, background_color);
  avatar.setColorPalette(cl);
}

void param_parse(const String& req){
  Serial.println("Params: " + req);
  constexpr int maxcmdlen = 2;
  constexpr int maxparamlen = 6;
  int basepos=0,pos=0;
  int len=req.length();
  String cmd,param;
  while(pos<len){
    while(!isAlpha(req.charAt(pos)) && pos<len) pos++; // skip to the next alphabet
    basepos = pos;
    while(isAlpha(req.charAt(pos)) && pos<len && pos-basepos<maxcmdlen) pos++;
    cmd = req.substring(basepos,pos);

    while(!(isDigit(req.charAt(pos))||req.charAt(pos)=='-') && pos<len) pos++; // skip to the next number
    basepos = pos;
    while((isDigit(req.charAt(pos))||req.charAt(pos)=='-') && pos<len && pos-basepos<maxparamlen) pos++;
    param = req.substring(basepos,pos);

    Serial.println("SetParamRaw: " + cmd + ":" + param);
    set_param(cmd, param.toInt());
  }
}

void request_parse(const String& req){
    Serial.printf("Requested:%s\n",req.c_str());
    if (req == "speaktest") {
      baloon_speak("Hello!");
    } else if (req.startsWith("move")) {
      servo_enabled=true;
      baloon_speak("move start");
    } else if (req == "nomove") {
      servo_enabled=false;
      baloon_speak("move stop");
    } else if (req == "homepos") {
      moveXY(90, 90, 1500);
    } else if (req == "pstore") {
      store_params();
      baloon_speak("Stored!");
    } else if (req == "pclear") {
      clear_params();
      baloon_speak("Cleared!");
    } else if (req.startsWith("rotate")) {
      Serial.println("Rotate anim start");
      time_rotating=millis();
      rotating_ccw = (req.charAt(6)=='r')? 1 : 0;
    } else if (req.startsWith("scale")) {
      Serial.println("Scale anim start");
      time_scaling=millis();
    } else if (req.startsWith("speak=")) {
      constexpr int bufsize=64;
      static char text[bufsize];
      url_decode(text,req.substring(req.indexOf('=')+1).c_str(),bufsize);
      baloon_speak(text);
    } else if (req.startsWith("expression=")) {
      set_expression(req.substring(req.indexOf('=')+1).toInt());
    } else if (req.startsWith("color=")) {
      set_color(req.substring(req.indexOf('=')+1).c_str());
    } else if(req.startsWith("params=")){
      param_parse(req.substring(7));
    }
}

void wifisetting_parse(const String& req){
  constexpr int bufsize=32;
  static char buf[bufsize];

  url_decode(buf,req.c_str(),bufsize);
  String param = buf;
  
  Serial.println("WiFiSettings: "+param);
  int pos = param.indexOf(':');
  if(pos>2){
    String ssid_v = param.substring(0,pos);
    String pass_v = param.substring(pos+1);
    strncpy(ssidbase,ssid_v.c_str(), wifiap_len);    
    strncpy(password,pass_v.c_str(), wifiap_len);   
    store_wifiap_params();
    baloon_speak("WiFi settings updated");
  }
}


/************************************************/
// ==== web server functions === 

void setup_server() {
  Serial.println();
  Serial.println("Configuring access point...");

  // You can remove the password parameter if you want the AP to be open.
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.begin();
  dnsServer.start(53, "*", myIP);

  Serial.println("Server started");
}

void output_htmloutput_header(WiFiClient& client){
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println();
}

void output_settingpage_contents(WiFiClient& client){
    client.print(
"<head><meta charset=\"utf-8\"></head>"
"<body>"
    );
    client.printf("<h2>StackChan:%s</h2>\n",stackchan_id);
    client.print(
"<ul>"
"  <li><a href=\"/c?move\">move start</a>  "
"  <li><a href=\"/c?nomove\">move stop</a>"
"  <li><a href=\"/c?homepos\">go to home position</a>"
"  <li><a href=\"/c?speaktest\">speak hello<a>"
"  <li><form action=\"/c\">speak (Eng/Jpn) <input type=\"text\" name=\"speak\"></form>"
"  <li>Change face: <a href=\"/c?params=fa0 \">Default</a>, "
                  " <a href=\"/c?params=fa1 \">DogFace</a>"
"  <li>Color: <a href=\"/c?color=FFFF,0\">Black(default)</a>, "
           " <a href=\"/c?color=FFE0,001F\">Blue</a>, "
           " <a href=\"/c?color=07FF,F800\">Red</a>, "
           " <a href=\"/c?color=F81F,07E0\">Green</a>, "
           " <a href=\"/c?color=001F,FFE0\">Yellow</a>, "
           " <a href=\"/c?color=07E0,F81F\">Purple</a>, "
           " <a href=\"/c?color=F800,07FF\">Cyan</a>, "
           " <a href=\"/c?color=0,FFFF\">White</a>,"
           " <form action=\"/c\">manual <input type=\"text\" name=\"color\" value=\"0,FFFF\"></form>"
"  <li>Expression: <a href=\"/c?expression=0\">Neutral</a>,"
                 " <a href=\"/c?expression=1\">Happy</a>,"
                 " <a href=\"/c?expression=2\">Sleepy</a>,"
                 " <a href=\"/c?expression=3\">Doubt</a>,"
                 " <a href=\"/c?expression=4\">Angry</a>,"
                 " <a href=\"/c?expression=5\">Sad</a>"
"  <li><a href=\"/c?rotate \">Rotate face(cw)</a>, "
     " <a href=\"/c?rotater \">Rotate face(ccw)</a>"
"  <li><a href=\"/c?scale \">Scaling anim</a>, "
"  <li><form action=\"/c\">param string <input type=\"text\" name=\"params\" placeholder=\"xo0yo0xl45xu135yl60yu90eb3ef10fq100ss15\"></form>"
"  <li>Moving Frequency <a href=\"/c?params=fq10 \">10*more</a>"
     " <a href=\"/c?params=fq100 \">default</a>"
     " <a href=\"/c?params=fq1000 \">10*less</a>"
"  <li>SD enabling <a href=\"/c?params=sd1\">enable</a>"
     " <a href=\"/c?params=sd0\">disable</a> (need save and reset)"
     " <a href=\"/sd/\">index.html on SD</a>"
"  <li><a href=\"/c?pstore\">save params</a> ,"
     " (<a href=\"/c?pclear\">clear the stored</a>) "
"</ul>"
"<hr>"
"Move Servo (CAUTION: this may break your stackchan. No range limit applied) <br>"
"The below values do not reflect the real value. <br>"
 "<form action=\"/m\">"
  "X: <input type=\"range\" name=\"x\" value=\"90\" min=\"0\" max=\"180\" oninput=\"document.getElementById('sx').value=this.value\">"
  "<output id=\"sx\">90</output><br>"
  "Y: <input type=\"range\" name=\"y\" value=\"90\" min=\"0\" max=\"180\" oninput=\"document.getElementById('sy').value=this.value\">"
  "<output id=\"sy\">90</output><br>"
  "<input value=\"move!\" type=\"submit\">"
"</form>"
"<hr>"
"<a href=\"/w?\">wifi settings</a> : "  
"<a href=\"/r?RESET\">Reset stackchan</a>"
"<hr>"
  );
}

void output_currentsettings(WiFiClient& client){
  client.print("<hr>");
  client.print("<h3>settings</h3>");
  client.printf("[px,py]x,y pin for servo: %d,%d<br>\n",servo_pin_x,servo_pin_y);
  client.printf("[xo,yo]x,y offset: %d,%d<br>\n",servo_offset_x,servo_offset_y);
  client.printf("[xl,xu]x range: %d - %d<br>\n",servo_x_min,servo_x_max);
  client.printf("[yl,yu]y range: %d - %d<br>\n",servo_y_min,servo_y_max);
  client.printf("[eb,ef]ease delay: base %d : factor %d<br>\n",easedelay_base,easedelay_factor);
  client.printf("[fq]move freq: %d  (smaller more frequent<br>\n",move_freq);
  client.printf("[fa]face type: %d <br>\n",selected_face);
  client.printf("[ss]speak speed: %d <br>\n",speak_speed);
  client.printf("[sd]SD Card(0=disable): %d <br>\n",sd_enabled);
  client.printf("[wf]WiFiAP (0=disabled): %d <br>\n",wifi_enabled);
  client.printf("Color(pr,bk): %x,%x <br>\n",primary_color, background_color);
}

void output_wifisetting_page(WiFiClient& client){
  client.println(
    "<html><head></head>"
    "<body><h3>WiFi setting</h3>"
      "To apply the change, reset your stackchan.<br>"
      "This message is always same and it does not mean the previous change is successful."
    "<hr>"
      "The real ssid is trailing with the last 2bytes of your Stackchan's MAC address." 
      "<form action=\"/w\">"
        "Set ssid:passphrase, delimiter is \":\"."
        "<input type='text' name='set' placeholder='");
  client.printf("%s:yourPassword",ssidbase);
  client.println("'>"
      "</form></body></html>");
}

void output_setting_json(WiFiClient& client){
  client.println("{");
  client.printf(" \"servo_offset\" : [%d,%d],\n",servo_offset_x,servo_offset_y);
  client.printf(" \"servo_x_range\" : [%d,%d],\n",servo_x_min,servo_x_max);
  client.printf(" \"servo_y_range\" : [%d,%d],\n",servo_y_min,servo_y_max);
  client.printf(" \"easedelay\" : [%d,%d],\n",easedelay_base,easedelay_factor);
  client.printf(" \"move_freq\" : %d,\n",move_freq);
  client.printf(" \"speak_speed\" : %d,\n",speak_speed);
  client.printf(" \"colors\" : [%d , %d],\n",primary_color, background_color);
  client.printf(" \"face\" : %d,\n",selected_face);
  client.printf(" \"sd_enable\": %d, \n",sd_enabled);
  client.printf(" \"wifi_enabled\": %d, \n",wifi_enabled);
  client.println("}");
}

bool output_sdcard_contents(WiFiClient& client,const String& file){
  Serial.println("SD access ["+file+"]");
  const char* dataType="text/plain";

  String path = file.substring(2);
  path.toLowerCase();

  if (path.endsWith("/")) {
    path += "index.html";
  }

  if (path.endsWith(".html") || path.endsWith(".htm")) {
    dataType = "text/html";
  } else if (path.endsWith(".css")) {
    dataType = "text/css";
  } else if (path.endsWith(".js")) {
    dataType = "application/javascript";
  } else if (path.endsWith(".txt")) {
    dataType = "text/plain";
  } else if (path.endsWith(".jpg")) {
    dataType = "image/jpeg";
  } else if (path.endsWith(".png")) {
    dataType = "image/png";
  } else {
    dataType = "application/octet-stream";
  }
  File dataFile = SD.open(path.c_str());
  if(dataFile.isDirectory()) {
    path+="/index.html";
    dataType = "text/html";
    dataFile = SD.open(path.c_str());
  }
  String contentType=dataType;
  if(dataFile) {
    Serial.println("SD File:"+path + " " + contentType);
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type: "+ contentType);
    client.println();

    while(dataFile.available()){
//      client.write(dataFile.read()); // too slow for me
        uint8_t buf[256];
        int len = dataFile.read(buf,sizeof(buf));
        client.write(buf,len);
    }
    dataFile.close();
    return true;
  }
  return false;
}

void output_page(WiFiClient& client, String& file){
  switch(file.charAt(0)){  // VERY LOOSE LOGIC
  case 's': // this should be a request for sdcard
    if(output_sdcard_contents(client,file)) break;
  case 'c':
    // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
    // and a content-type so the client knows what's coming, then a blank line:
    output_htmloutput_header(client);
      // the content of the HTTP response follows the header:
    output_settingpage_contents(client);
    output_currentsettings(client);
      // The HTTP response ends with another blank line:
    client.println();
    break;
  case 'w':
    output_htmloutput_header(client);
    output_wifisetting_page(client);
    break;
  case 'q':
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/json");
    client.println();
    output_setting_json(client);
    break;
  case 'n':
     // no output
    output_htmloutput_header(client);
    client.println();
     break;
  case 'a':
    output_htmloutput_header(client);
    client.println("<body><h3>Another world</h3>");
    client.println("You must be punished going to prohibited area.</body>");
    break;
  default:
    output_htmloutput_header(client);
    client.println("<body><h3>Unprecedented world</h3>Hmm....</body>");
    break;
  }
}

void moveservo_parse(const String& req) {
  String p1=req.substring(2,req.indexOf('&'));
  String p2=req.substring(req.indexOf('&')+3);
  Serial.println("Move: " + p1 + "," + p2);
  moveXY(p1.toInt(),p2.toInt(),1500);
}

const char* process_request(const String& req){
  const char* ret="c";
  Serial.println("processing type:"+req.substring(0,2));
  if(req.substring(0,2)=="c?") {  // requested url is setting page
    if(req.length()>0) request_parse(req.substring(2));  // else do nothing
  } else if(req.substring(0,2)=="d?") {  // request without html page
    if(req.length()>0) request_parse(req.substring(2));
    ret="n";
  } else if(req.substring(0,2)=="w?") {
    wifisetting_parse(req.substring(6));
    ret="w";
  } else if(req.substring(0,2)=="m?") {
    moveservo_parse(req.substring(2));
  } else if(req.substring(0,3)=="qp") {
    ret="q";
  } else if(req.substring(0,3)=="sd/") {
    ret=req.c_str();
  } else if(req=="an") {
    ret="a";
  } else if(req.startsWith("r?RESET")) {
    Serial.println("ESP32 Reset");
    ESP.restart();
  }
  return ret;
}

void loop_server() {
  WiFiClient client = server.available();   // listen for incoming clients

  if (client) {                             // if you get a client,
    Serial.println("New Client.");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    String requestText="";
    bool need_redirect=false;
    String file="c";  // setting page by default
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
//        Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character
          Serial.printf("Line:%s\n",currentLine.c_str());

          String cl_h5=currentLine.substring(0,5);  // heading 5chars of currentLine
          cl_h5.toLowerCase();
          if(cl_h5=="get /"){  // url , do not mind in case of many space chars
                // argument to cut the hedding "get /" and trailing "HTTP/1.1"
            file=process_request(currentLine.substring(5,currentLine.lastIndexOf(' ')));
          } else if(cl_h5 == "host:"){                              // requested host
            if(currentLine.substring(6) != stackchan_url){
//              Serial.println("Not StackchanHost");
              need_redirect=true;
            }
          }
          if(currentLine.length()==0){
              // break out of the while loop:;
              break;
          }
          currentLine="";
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }

    // make response
    if(need_redirect) {
      Serial.println("HTTP REDIRECTED");
      output_htmloutput_header(client);
      client.println("<html><head>");
      client.printf("<meta http-equiv=\"refresh\" content=\"0; url='http://%s'\" />\n",stackchan_url);
      client.println("</head></html>");
    } else {
      output_page(client,file);
    }
    // close the connection:
    client.stop();
    Serial.println("Client Disconnected.");
  }
}

/************************************************/
// ==== serial ====

template <class T> void loop_serial(T& ser) {
  static String linebuf="";
  static int len=0;
  while(ser.available()){
    char c = ser.read();
    if (c == '\n') {      
      ser.println("Serial Text:" + linebuf);
      if(linebuf.charAt(1)=='?') {  // requested url is setting page
        process_request(linebuf);
      } else if(linebuf=="settings"){
        ser.println("Setting values");
        ser.printf("servo_offset_x/y: [%d,%d]\n",servo_offset_x,servo_offset_y);
        ser.printf("servo_x_min/max : [%d,%d]\n",servo_x_min,servo_x_max);
        ser.printf("servo_y_min/max : [%d,%d]\n",servo_y_min,servo_y_max);
        ser.printf("ease delay time : 100*(%d +rnd(0-%d)) ms\n",easedelay_base,easedelay_factor);
        ser.printf("move frequency  : %d\n",move_freq);
        ser.printf("speak speed     : %d\n",speak_speed);
        ser.printf("face#           : %d\n",selected_face);
        ser.printf("color pri/bak   : [%d , %d]\n",primary_color, background_color);
        ser.printf("SD Card         : %s\n",(sd_enabled)?"enabled":"disabled");
        ser.printf("WiFiAP          : %s\n",(wifi_enabled)?"enabled":"disabled");
      }
      linebuf="";
    } else if (c != '\r') { 
      linebuf += c;  
    }
  }
}


/************************************************/
// ==== setup and loop === 

void setup() {
  M5.begin();

  // for avatar
  avatar.init();

  load_params();
  faces[0]=avatar.getFace();
  faces[1]=new DogFace();
  avatar.setFace(faces[selected_face]);

  last_mouth_millis = millis();  

  auto mac = WiFi.macAddress();
  snprintf(stackchan_id,sizeof(stackchan_id)/sizeof(*stackchan_id),"%2x%2x",mac[0],mac[1]);
  snprintf(ssid,wifiap_len,"%s%s",ssidbase,stackchan_id);

  BtSerial.begin(ssid);   //reuse ssid as bluetooth device name

  setup_servos();

  if(wifi_enabled) setup_server();
  randomSeed(millis());

  if(sd_enabled!=0) {
    Serial.println("SD initialization start");
    int i; constexpr int maxi=5;
    for(i=0; i<maxi && !SD.begin(GPIO_NUM_4, SPI); i++) delay(100);
    Serial.printf("SD initialization end(%s)\n",(i<maxi)? "successful":"failed");
    if(i==maxi) baloon_speak("No SD card");
  }

  // to display Japanese text
  M5.Lcd.setFont(&fonts::efontJA_12);
  avatar.setSpeechFont(&fonts::efontJA_12);
}

void suspenOrResumedStackchanTasks(bool want_suspend){
  static TaskHandle_t drawTaskHandle=0;
  if(drawTaskHandle==0) drawTaskHandle = xTaskGetHandle("drawLoop");

  if(want_suspend){
    eTaskState s;
    while((s=eTaskGetState(drawTaskHandle)) != eSuspended){
//      Serial.printf("waiting thread suspended[%d]\n",s);
      vTaskSuspend(drawTaskHandle);  
      delay(50);
    }
  } else {
    if(drawTaskHandle!=0) vTaskResume(drawTaskHandle);  // reversed order
  }
}


void loop() {
  M5.update();
  if(wifi_enabled) {
    loop_server();
    dnsServer.processNextRequest();
  }
  loop_serial<HardwareSerial>(Serial);
  loop_serial<BluetoothSerial>(BtSerial);
  loop_avatar();

  if(M5.BtnA.wasReleased()){   // BtnA for move/nomove
    servo_enabled = !servo_enabled;
    baloon_speak((servo_enabled)? "move start" : "move stop");
    Serial.printf("servo_enabled: %s\n" , ((servo_enabled)? "true" : "false"));
  } else if(M5.BtnA.pressedFor(5000)){ // long press of BtnA for toggle wifi_enabled and reset.
    wifi_enabled = (wifi_enabled)? 0 : 1;
    preference.begin(pref_stackchan, false);
    preference.putInt("wf_enable",wifi_enabled);
    preference.end();
    char buf[32];
    snprintf(buf,sizeof(buf),"resetting w/ wifi %s",(wifi_enabled)?"enabled":"disabled");
    baloon_speak(buf);
    Serial.println(buf);
    delay(2000);
    ESP.restart();
  }

  if(M5.BtnB.wasReleased()) {  // BtnB for QR code
    suspenOrResumedStackchanTasks(true);
 
    char buf[128];
    snprintf(buf,128,"WIFI:T:WPA;S:%s;P:%s",ssid,password);
    M5.Lcd.qrcode(buf,70,0,180,5);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(156,215);
    M5.Lcd.print("v");
    M5.Lcd.setCursor(20,180);
    M5.Lcd.println("Connect with this QRcode");
    M5.Lcd.println("    Or press | to exit");
    while(!server.available()){
      delay(100);
      M5.update();
      if(M5.BtnB.wasReleased())break;
    }
    suspenOrResumedStackchanTasks(false);
  }

  static bool param_cleared=false;
  if(M5.BtnC.pressedFor(5000)){ // long press of BtnC for clear parameters.
    if(!param_cleared) {
      clear_params();
      baloon_speak("cleared!");
      param_cleared=true;
    }
  } if(M5.BtnC.pressedFor(10000)){ // long press of BtnC for clear wifi parameters.
      clear_wifiparams();
      ESP.restart();          
  } else if(M5.BtnC.wasReleased()) {
    param_cleared=false;
  }
  delay(50);
}
