#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <functional>
#include <set>

#define WIFI_JSON_PATH "/wifi.json"

class WiFiManager
{
private:
    String APNamePrefix;
    bool AddMacAddressToAPName;
    String ApplicationName;
    String AckMessage;
    AsyncWebServer webServer;
    String SSID, Password, Owner;
    unsigned long gatherTimeOutMS;

    static WiFiManager *singleton;
    enum STATE { STATE_NONE, STATE_WAITING_SSID, STATE_WAITING_URL, STATE_COMPLETE };
    volatile STATE state;

public:
    WiFiManager(int gatherPort = 80, bool addMacAddressToSSID = false, int gatherTimeoutSecs = 300) :  
        webServer(gatherPort),
        AddMacAddressToAPName(addMacAddressToSSID),
        gatherTimeOutMS(gatherTimeoutSecs * 1000UL)
    {
        singleton = this; 
        initSpiffs(false); 
    }

    virtual bool OnRequestConnectToSSID(const char *SSID) const  { return true; }
    virtual bool OnRequestConnectToURL(const char *URL) const { return true; }
    virtual bool OnWaitForSSIDConnect() const { return true; }
    virtual bool OnWaitForURLConnect() const { return true; }
    virtual void OnConnecting() const {}
    virtual void OnGatheringComplete() const {}
    virtual void OnWiFiEvent(WiFiEvent_t event) const {}

    String GetSSID() { return SSID; }
    String GetPassword() { return Password; }
    String GetOwner() { return Owner; }

    void ProvisionCredentials(const char *ssid, const char *pass, const char *owner="")
    {
        log_i("Trying to provision %s/****\n", ssid);
        DynamicJsonDocument dict(10000);
        LoadJsonSsidDictionary(dict);
        dict[ssid]["Password"] = pass;
        dict[ssid]["Owner"] = owner;
        SaveJsonSsidDictionary(dict);
    }

    bool Connect()
    {
        DynamicJsonDocument dictionary(10000);

        log_i("Connect\n");
        log_i(" - Scan networks\n");
        unsigned long start = millis();

        WiFi.scanNetworks(true);
        int networksFound;
        while ((networksFound = WiFi.scanComplete()) < 0)
        {
            this->OnConnecting();
        }
        log_i(" - Scan completed in %lu ms, returns %d networks found\n", millis() - start, networksFound);
        
        if (!LoadJsonSsidDictionary(dictionary))
        {
            log_i(" - SSID Dictionary load failed\n");
            return false;
        }

        log_i(" - %d WiFi networks compared to DB of %d entries\n.", networksFound, dictionary.size());
        bool matchFound = false;
        for (int i=0; i<networksFound; ++i)
        {
            auto val = dictionary[WiFi.SSID(i)];
            if (!val.isNull() && !val["Password"].isNull())
            {
                this->SSID = WiFi.SSID(i);
                this->Password = val["Password"].as<const char *>();
                this->Owner = val["Owner"].isNull() ? "" : val["Owner"].as<const char *>();
                matchFound = true;
                break;
            }
        }

        if (!matchFound)
        {
            log_i(" - failed.  (No matching SSID found)\n");
            return false;
        }

        log_i(" - Trying to start WiFi with %s/****... ", this->SSID.c_str());

        WiFi.begin(this->SSID.c_str(), this->Password.c_str());
        for (uint64_t start = millis(); !WiFi.isConnected() && !WiFiConnectionFailed() && millis() - start < 30000;)
        {
            this->OnConnecting();
        }

        if (WiFi.isConnected())
            log_i(" - success.\n");
        else
            log_i(" - fail.\n");

        return WiFi.isConnected();
    }

    bool GatherCredentials(
        const char *APNamePrefix = "ESP32-WiFi", 
        const char *ApplicationName = "ESP32-WiFi",
        const char *AckMessage = "<p>Your input has been received. Thank you.</p>"
    )
    {
        log_i("Beginning GatherCredentials\n");
        this->APNamePrefix = APNamePrefix;
        this->ApplicationName = ApplicationName;
        this->AckMessage = AckMessage;
        this->state = STATE_NONE;

        WiFi.onEvent(WiFiEventHandler);
        webServer.on("/", HTTP_GET, HandleGetRoot);
        webServer.on("/", HTTP_POST, HandlePostRoot);

        log_i("Entering GetWiFiCredentials\n");

        String ApName = String(APNamePrefix);
        if (AddMacAddressToAPName)
        {
            ApName += String("-") + WiFi.macAddress().c_str();
            ApName.replace(":", "-");
        }

        // Connect to Wi-Fi network with SSID and password
        log_i("Setting AP (Access Point) %s\n", ApName.c_str());

        // NULL sets an open Access Point
        if (!WiFi.softAP(ApName.c_str(), NULL))
            log_i(" - SoftAP failed\n");

        IPAddress IP = WiFi.softAPIP();
        log_i("AP IP address: %s\n", IP.toString().c_str());

        webServer.begin();

        state = STATE_WAITING_SSID;
        log_i("Prompt: Connect to WiFi '%s'\n", ApName.c_str());
        bool end = !this->OnRequestConnectToSSID(ApName.c_str());
        for (unsigned long start = millis(); !end && millis() - start < this->gatherTimeOutMS; )
        {
            int nClients = WiFi.softAPgetStationNum();
            if (nClients == 0 && state == STATE_WAITING_URL) // client disconnected?
            {
                log_i("Prompt: Connect to WiFi '%s'\n", ApName.c_str());
                state = STATE_WAITING_SSID;
                end = !this->OnRequestConnectToSSID(ApName.c_str());
            }
            else if (nClients == 1 && state == STATE_WAITING_SSID)
            {
                log_i(" - someone connected to WiFi AP\n");
                String URL = "http://" + WiFi.softAPIP().toString() + "/";
                log_i("Prompt: Connect to this URL: %s\n", URL.c_str());
                state = STATE_WAITING_URL;
                end = !this->OnRequestConnectToURL(URL.c_str());
            }
            else if (state == STATE_COMPLETE)
            {
                end = true;
            }
            if (!end && state == STATE_WAITING_SSID)
                end = !this->OnWaitForSSIDConnect();
            if (!end && state == STATE_WAITING_URL)
                end = !this->OnWaitForURLConnect();
        }
        webServer.end();
        return state == STATE_COMPLETE;
    }

    void Disconnect()
    {
        WiFi.removeEvent(WiFiEventHandler);
        webServer.reset();
        WiFi.disconnect(true); // https://stackoverflow.com/questions/61890377/esp32-using-ble-and-wifi-alternately
        WiFi.mode(WIFI_OFF);
    }

    bool EraseCredentials()
    {
        if (!SPIFFS.remove(WIFI_JSON_PATH))
        {
            initSpiffs(true);
        }
        return true;
    }

    static String ReadFile(const char *path)
    {
        log_i(" - Reading file: %s\r\n", path);

        fs::File file = SPIFFS.open(path);
        if (!file || file.isDirectory())
        {
            Serial.printf(" - failed to open file for reading\n");
            return String();
        }

        String fileContent;
        while (file.available())
        {
            fileContent = file.readStringUntil('\n');
            break;     
        }
        return fileContent;
    }

    // Write file to SPIFFS
    static void WriteFile(const char *path, const char *message)
    {
        Serial.printf(" - Writing file: %s", path);

        fs::File file = SPIFFS.open(path, FILE_WRITE, true);
        if (file && file.print(message))
        {
            Serial.printf(" - success\n");
            file.close();
        }
        else
        {
            Serial.printf(" - fail\n");
        }
    }

private:
    static void initSpiffs(bool formatNeeded = false)
    {
        if (!formatNeeded)
        {
            log_i("Initializing SPIFFS...\n");
            if (!SPIFFS.begin())
            {
                log_i(" - failed: trying formatting\n");
                formatNeeded = true;
            }
        }

        if (formatNeeded)
        {
            log_i("Formatting SPIFFS...\n");
            if (!SPIFFS.format() || !SPIFFS.begin(true))
            {
                log_i(" - failed.\n");
            }
        }
    }

    static void WiFiEventHandler(WiFiEvent_t event)
    {
        const char * arduino_event_names[] = {
            "WIFI_READY",
            "SCAN_DONE",
            "STA_START", "STA_STOP", "STA_CONNECTED", "STA_DISCONNECTED", "STA_AUTHMODE_CHANGE", "STA_GOT_IP", "STA_GOT_IP6", "STA_LOST_IP",
            "AP_START", "AP_STOP", "AP_STACONNECTED", "AP_STADISCONNECTED", "AP_STAIPASSIGNED", "AP_PROBEREQRECVED", "AP_GOT_IP6", 
            "FTM_REPORT",
            "ETH_START", "ETH_STOP", "ETH_CONNECTED", "ETH_DISCONNECTED", "ETH_GOT_IP", "ETH_GOT_IP6",
            "WPS_ER_SUCCESS", "WPS_ER_FAILED", "WPS_ER_TIMEOUT", "WPS_ER_PIN", "WPS_ER_PBC_OVERLAP",
            "SC_SCAN_DONE", "SC_FOUND_CHANNEL", "SC_GOT_SSID_PSWD", "SC_SEND_ACK_DONE",
            "PROV_INIT", "PROV_DEINIT", "PROV_START", "PROV_END", "PROV_CRED_RECV", "PROV_CRED_FAIL", "PROV_CRED_SUCCESS"
        };
        log_i(" - WiFi event: %d %s\n", event, arduino_event_names[event]);
        singleton->OnWiFiEvent(event);
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
        {
            log_i("** WiFi disconnected: shutting down\n");
        }
    }
   
    static void HandleGetRoot(AsyncWebServerRequest *request)
    {
        request->send(200, "text/html", CreateWiFiManagerHtml());
    }

    static bool LoadJsonSsidDictionary(DynamicJsonDocument &doc)
    {
        // Load credentials from SPIFFS
        String dictionaryString = ReadFile(WIFI_JSON_PATH);
        DeserializationError ret = deserializeJson(doc, dictionaryString);
        if (ret != 0)
        {
            log_i(" - Dictionary enumeration failed\n");
            return false;
        }
        log_i(" - SSID dictionary is %s\n", dictionaryString.c_str());
        return true;
    }

    static void SaveJsonSsidDictionary(DynamicJsonDocument &doc)
    {
        String serialized;
        int len = serializeJson(doc, serialized);
        log_i(" - serializeJson SSIDs returns length of %d\n", len);
        log_i(" - serialized dict is %s\n", serialized.c_str());
        WriteFile(WIFI_JSON_PATH, serialized.c_str());
    }

    static void HandlePostRoot(AsyncWebServerRequest *request)
    {
        int params = request->params();
        log_i(" - POST Request received: %s\n", request->url().c_str());
        String ssid, pass, owner;

        for (int i=0;i<params;i++) {
            AsyncWebParameter* p = request->getParam(i);
            if (p->isPost())
            {
                log_i(" - Param: %s\n", p->name().c_str());
                // HTTP POST ssid value
                if (p->name() == "ssid") {
                    ssid = p->value().c_str();
                    log_i(" - SSID set to: %s\n", ssid.c_str());
                }
                // HTTP POST pass value
                if (p->name() == "pass") {
                    pass = p->value().c_str();
                    log_i(" - Password set to: %s\n", pass.c_str());
                }
                // HTTP POST owner value
                if (p->name() == "owner") {
                    owner = p->value().c_str();
                    log_i(" - Owner set to: %s\n", owner.c_str());
                }
            }
        }

        DynamicJsonDocument dict(10000);
        LoadJsonSsidDictionary(dict);
        dict[ssid.c_str()]["Password"] = pass.c_str();
        dict[ssid.c_str()]["Owner"] = owner.c_str();
        SaveJsonSsidDictionary(dict);

        String ack = CreateAcknowledgeHtml();
        request->send(200, "text/html", ack.c_str());
        singleton->OnGatheringComplete();
        singleton->state = STATE_COMPLETE;
    }

    static bool WiFiConnectionFailed()
    {
        return WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_CONNECTION_LOST/* || WiFi.status() == WL_DISCONNECTED*/;
    }

    static String CreateWiFiManagerHtml()
    {
        const char* htmlTemplate = R"(
            <!DOCTYPE html>
            <html lang="en">
            <head>
                <meta charset="UTF-8">
                <meta name="viewport" content="width=device-width, initial-scale=1.0">
                <title>%APPLICATION% WiFi Manager</title>
                <style>
                    body {
                        font-family: Arial, sans-serif;
                        text-align: center;
                        margin: 20px;
                    }
                    
                    form {
                        display: inline-block;
                        text-align: left;
                        padding: 20px;
                        border: 1px solid #ccc;
                        border-radius: 4px;
                        box-shadow: 0 0 5px rgba(0, 0, 0, 0.1);
                    }
                    
                    label {
                        display: block;
                        margin-bottom: 5px;
                    }
                    
                    select, input[type="password"], input[type="text"] {
                        width: 100%;
                        padding: 8px;
                        margin-bottom: 10px;
                        border: 1px solid #ccc;
                        border-radius: 4px;
                    }
                    
                    input[type="submit"] {
                        width: auto;
                        padding: 10px 20px;
                        background-color: #4CAF50;
                        color: white;
                        border: none;
                        border-radius: 4px;
                        cursor: pointer;
                    }
                </style>
            </head>
            <body>
                <h1>%APPLICATION% needs Wi-Fi Credentials</h1>
                <form method="POST">
                    <label for="ssid">WiFi SSID</label>
                    <select id="ssid" name="ssid">%SSIDS%
                    </select>
                    
                    <label for="pass">Password</label>
                    <input type="password" id="pass" name="pass">
                    
                    <label for="owner">Device Owner</label>
                    <input type="text" id="owner" name="owner">
                    
                    <input type="submit" value="Submit">
                </form>
            </body>
            </html>
        )";

        // Create SSID list for pulldown
        int nNetworks = WiFi.scanComplete();
        if (nNetworks < 1)
            nNetworks = WiFi.scanNetworks();
        std::set<String> networks;
        for (int i=0; i<nNetworks; ++i)
            networks.insert(WiFi.SSID(i));
        String SSIDHtml = "\r\n                        <option value=\"noselection\"></option>\r\n";

        for (String str: networks)
            SSIDHtml += "                        <option value=\"" + str + "\">" + str + "</option>\r\n";
        
        String ret = htmlTemplate;
        ret.replace("%APPLICATION%", singleton->ApplicationName);
        ret.replace("%SSIDS%", SSIDHtml);
        return ret;
    }

    static String CreateAcknowledgeHtml()
    {
        const char *htmlTemplate = R"(
            <!DOCTYPE html>
            <html lang="en">
            <head>
                <meta charset="UTF-8">
                <meta name="viewport" content="width=device-width, initial-scale=1.0">
                <title>%APPLICATION% Wi-Fi Manager</title>
                <style>
                    body {
                        font-family: Arial, sans-serif;
                        text-align: center;
                        margin: 20px;
                    }
                    
                    .message {
                        margin-top: 100px;
                    }
                </style>
            </head>
            <body>
                <div class="message">
                    %ACKMESSAGE%
                </div>
            </body>
            </html>
        )";

        String ret = htmlTemplate;
        ret.replace("%APPLICATION%", singleton->ApplicationName);
        ret.replace("%ACKMESSAGE%", singleton->AckMessage);
        return ret;
    }
};

WiFiManager *WiFiManager::singleton = NULL;
