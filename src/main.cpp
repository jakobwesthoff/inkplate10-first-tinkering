// Next 3 lines are a precaution, you can ignore those, and the example would also work without them
#ifndef ARDUINO_INKPLATE10
#error "Wrong board selection for this example, please select Inkplate 10 in the boards menu."
#endif

#define FS_NO_GLOBALS

#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>

#include "Inkplate.h"

// WIFI config
const char *softap_ssid = "ESP32-Access-Point";
const char *ssid = "Exiel";
const char *password = "felix_kommt_nicht_ins_wlan";

AsyncWebServer server(80);
Inkplate display(INKPLATE_3BIT);

void *body = NULL;
size_t body_size = 0;
bool should_render = false;

void initWiFi();
void initWiFiSoftAp();
size_t http_request(char *url, byte *buffer, size_t buffer_size);
void render(uint8_t *raw_image, size_t nBytes);

void setup()
{
  Serial.begin(115200);

  // Check PSRAM is working
  log_d("Total heap: %d", ESP.getHeapSize());
  log_d("Free heap: %d", ESP.getFreeHeap());
  log_d("Total PSRAM: %d", ESP.getPsramSize());
  log_d("Free PSRAM: %d", ESP.getFreePsram());

  initWiFi();

  // Initialize webserver
  server.on("/hello", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/plain", "Hello World"); });

  server.on(
      "/render", HTTP_POST, [](AsyncWebServerRequest *request)
      {
        log_d("render request received");
        request->send(200, "application/json", "{status: \"rendering\"}");
        should_render = true;
      },
      NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
      {
        log_d("body chunk received: len %d, index %d, total %d", len, index, total);
        if (index == 0)
        {
          // Beginning of body
          if (body != NULL)
          {
            free(body);
            body = NULL;
            log_d("Freed old body memory");
          }
          body = ps_malloc(total + 1);
          memset(body, 0, total + 1);

          if (body == NULL)
          {
            request->send(500, "text/plain", "Memory for body could not be allocated.");
            log_d("Couldn't allocate memory %d", total + 1);
            return;
          }
        }
        else
        {
          // Skip further chunks, if initial allocation failed.
          if (body == NULL)
          {
            return;
          }
        }

        memcpy(body + index, data, len);

        log_d("index + len (%d) == total (%d)", index + len, total);
        if (index + len == total)
        {
          // Body is complete
          body_size = total;
          log_d("Body complete!");
        }
      });

  server.begin();

  display.begin();
  display.clearDisplay();
  //(NOTE! This does not clean image on screen, it only clears it in the frame buffer inside ESP32).
  display.display();
  display.setTextColor(0, 7);
  display.setCursor(250, 420);
  display.setTextSize(4);
  display.println("Welcome to Inkplate 10!");
  display.print("SSID: ");
  display.println(ssid);
  display.display();

  delay(100);

  size_t buffer_size = E_INK_WIDTH * E_INK_HEIGHT / 2 + 1;
  byte *buffer = (byte *)ps_malloc(buffer_size);
  if (buffer == nullptr)
  {
    Serial.println("Could not allocate ram for request!");
    delay(1000);
    ESP.restart();
  }

  size_t received = http_request("http://192.168.178.49:8000/comic/inkplate", buffer, buffer_size);

  Serial.printf("Received bytes %d, expected %d\n", received, buffer_size - 1);

  if (received == buffer_size - 1)
  {
    Serial.printf("Rendering...");
    render(buffer, buffer_size - 1);
  }
}

void loop()
{
  if (should_render)
  {
    render((uint8_t *)body, body_size);
    should_render = false;
  }
}

void initWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void initWiFiSoftAp()
{
  Serial.print("Setting AP (Access Point)…");
  WiFi.softAP(ssid);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
}

size_t http_request(char *url, byte *buffer, size_t buffer_size)
{
  HTTPClient http;
  size_t bytes_read = 0;

  http.begin(url);

  Serial.print("[HTTP] GET...\n");
  // start connection and send HTTP header
  int httpCode = http.GET();
  if (httpCode > 0)
  {
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK)
    {
      int content_length = http.getSize();
      Serial.printf("Content-Length: %d\n", content_length);

      Serial.println("Starting receive.");

      WiFiClient *stream = http.getStreamPtr();
      while (http.connected() && (content_length == -1 || bytes_read < content_length))
      {
        Serial.printf("Waiting for chunk %d remaining from %d\n", content_length - bytes_read, content_length);
        size_t stream_avail = stream->available();
        if (stream_avail)
        {
        Serial.printf("Reading chunk %d\n", stream_avail);
          int last_read = stream->readBytes(buffer + bytes_read, (buffer_size - bytes_read) < stream_avail ? buffer_size - bytes_read : stream_avail);

          bytes_read += last_read;

          if (bytes_read == buffer_size)
          {
            Serial.println("Http receive buffer is full. (Maybe too small?)");
            http.end();
            return bytes_read;
          }
        }
        delay(1);
      }

      Serial.println("[HTTP] connection closed or file end.");
    }
  }
  else
  {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();

  return bytes_read;
}

void render(uint8_t *raw_image, size_t nBytes)
{
  size_t i;
  uint32_t x, y;
  for (i = 0; i < nBytes; i++)
  {
    y = i / (E_INK_WIDTH / 2);
    x = (i % (E_INK_WIDTH / 2)) * 2;
    display.drawPixel(x, y, (raw_image[i] >> 4) >> 1);
    display.drawPixel(x + 1, y, (raw_image[i] & 0x0f) >> 1);
  }
  display.display();
}
