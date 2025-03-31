/**
 * ESP32 Multi-Camera WebSocket Server
 * Created by: Mateusz Jachimowski
 * Last Updated: 2025-01-28
 * 
 * This system implements a WebSocket server that:
 * - Creates a WiFi Access Point for camera clients
 * - Manages multiple camera connections
 * - Handles real-time video streaming
 * - Processes motion detection events
 * - Serves a web interface for monitoring
 */

// Required libraries for async web server and WebSocket functionality
#include <ESPAsyncWebServer.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>

using namespace websockets;


// Network configuration
const char* ssid = "ESP32-AP"; // Access Point SSID
const char* password = "monitoring"; // Access Point password

// System constants
constexpr unsigned long CLIENT_TIMEOUT = 10000; // Client timeout in milliseconds
constexpr size_t MAX_CLIENTS = 2; // Maximum number of camera clients
constexpr size_t MAX_FRAME_SIZE = 16384; // Maximum frame size in bytes

// Web server instances
AsyncWebServer server(80); // HTTP server on port 80
WebsocketsServer wsServer; // WebSocket server for cameras
AsyncWebSocket wsBrowser("/ws"); // WebSocket endpoint for browser clients

/**
 * Static buffer for frame data
 * Uses a fixed size to prevent heap fragmentation
 */
struct StaticFrameBuffer {
    static constexpr size_t SIZE = MAX_FRAME_SIZE;
    uint8_t data[SIZE];
    size_t length = 0;
};

StaticFrameBuffer frameBuffer;

// HTML template stored in program memory (PROGMEM)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Camera Monitoring</title>
    <style>
        :root {
            --primary-color: #2c3e50;
            --accent-color: #3498db;
            --background-color: #f5f6fa;
            --text-color: #2c3e50;
            --shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
        }

        body {
            font-family: 'Segoe UI', Arial, sans-serif;
            margin: 0;
            padding: 20px;
            background-color: var(--background-color);
            color: var(--text-color);
        }

        h1 {
            text-align: center;
            color: var(--primary-color);
            margin-bottom: 30px;
            font-size: 2.2em;
            font-weight: 600;
        }

        .cameras-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 30px;
            max-width: 1400px;
            margin: 0 auto;
            padding: 0 15px;
        }

        .camera-container {
            background: white;
            border-radius: 12px;
            padding: 15px;
            box-shadow: var(--shadow);
            transition: transform 0.2s ease;
        }

        .camera-container:hover {
            transform: translateY(-5px);
        }

        .camera-container h2 {
            color: var(--primary-color);
            margin: 0 0 15px 0;
            font-size: 1.5em;
            font-weight: 500;
        }

        .motion-status {
            background-color: #2ecc71;
            color: white;
            padding: 8px 12px;
            border-radius: 6px;
            margin-bottom: 10px;
            text-align: center;
            font-weight: 500;
            transition: all 0.3s ease;
        }

        .motion-status.motion-active {
            background-color: #e74c3c;
            animation: pulse-bg 1s infinite;
        }

        @keyframes pulse-bg {
            0% { background-color: #e74c3c; }
            50% { background-color: #c0392b; }
            100% { background-color: #e74c3c; }
        }

        .image-wrapper {
            position: relative;
            border-radius: 8px;
            overflow: hidden;
            background: #000;
        }

        img {
            width: 100%;
            height: auto;
            display: block;
            transition: opacity 0.3s ease;
        }

        .status-overlay {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            background: rgba(44, 62, 80, 0.85);
            color: white;
            padding: 12px 20px;
            border-radius: 8px;
            font-weight: 500;
            display: none;
            animation: fadeIn 0.3s ease;
        }

        .motion-indicator {
            position: absolute;
            top: 10px;
            right: 10px;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background-color: #e74c3c;
            opacity: 0;
            transition: opacity 0.3s ease;
        }

        .motion-indicator.motion-active {
            opacity: 1;
            animation: pulse 1s infinite;
        }

        @keyframes pulse {
            0% { transform: scale(1); }
            50% { transform: scale(1.2); }
            100% { transform: scale(1); }
        }

        @keyframes fadeIn {
            from { opacity: 0; }
            to { opacity: 1; }
        }

        @media (max-width: 768px) {
            .cameras-grid {
                grid-template-columns: 1fr;
            }

            h1 {
                font-size: 1.8em;
            }

            .camera-container h2 {
                font-size: 1.3em;
            }
        }
    </style>
</head>
<body>
    <h1>Monitoring System</h1>
    <div class="cameras-grid">
        <div class="camera-container">
            <h2>Camera 1</h2>
            <div class="motion-status">No Motion Detected</div>
            <div class="image-wrapper">
                <img id="stream1" alt="Camera 1 offline">
                <div id="status1" class="status-overlay">Camera Disconnected</div>
                <div id="motion1" class="motion-indicator"></div>
            </div>
        </div>
        <div class="camera-container">
            <h2>Camera 2</h2>
            <div class="motion-status">No Motion Detected</div>
            <div class="image-wrapper">
                <img id="stream2" alt="Camera 2 offline">
                <div id="status2" class="status-overlay">Camera Disconnected</div>
                <div id="motion2" class="motion-indicator"></div>
            </div>
        </div>
    </div>

<script>
(function() {
    const ws = new WebSocket('ws://' + window.location.hostname + '/ws');
    ws.binaryType = 'arraybuffer';
    const imageUrls = new Map();
    
    ws.onmessage = function(event) {
        if (typeof event.data === 'string') {
            try {
                const message = JSON.parse(event.data);
                if (message.type === 'status') {
                    const statusDiv = document.getElementById('status' + message.camera);
                    const img = document.getElementById('stream' + message.camera);
                    
                    if (message.status === 'connected') {
                        if (statusDiv) statusDiv.style.display = 'none';
                        if (img) img.style.opacity = '1';
                    } else if (message.status === 'disconnected') {
                        if (statusDiv) {
                            statusDiv.textContent = 'Camera Disconnected';
                            statusDiv.style.display = 'block';
                        }
                        if (img) img.style.opacity = '0.3';
                    }
                } else if (message.type === 'motion') {
                    const cameraId = message.camera;
                    const motionIndicator = document.getElementById('motion' + cameraId);
                    const motionStatus = motionIndicator.parentElement.parentElement.querySelector('.motion-status');
                    
                    if (motionIndicator && motionStatus) {
                        if (message.detected) {
                            motionIndicator.classList.add('motion-active');
                            motionStatus.classList.add('motion-active');
                            motionStatus.textContent = 'Motion Detected!';
                            setTimeout(() => {
                              motionIndicator.classList.remove('motion-active');
                              motionStatus.classList.remove('motion-active');
                              motionStatus.textContent = 'No Motion Detected';
                               const resetMsg = {
                                type: 'motion',
                                camera: cameraId,
                                detected: false
                               };
                               ws.send(JSON.stringify(resetMsg));
                            }, 2000);
                        } else {
                            motionIndicator.classList.remove('motion-active');
                            motionStatus.classList.remove('motion-active');
                            motionStatus.textContent = 'No Motion Detected';
                        }
                    }
                }
            } catch (e) {
                console.error('Error parsing message:', e);
            }
            return;
        }

        const data = new Uint8Array(event.data);
        const cameraId = data[12];
        const img = document.getElementById('stream' + cameraId);
        if (!img) return;

        if (imageUrls.has(cameraId)) {
            URL.revokeObjectURL(imageUrls.get(cameraId));
        }
        
        const blob = new Blob([data], {type: 'image/jpeg'});
        const url = URL.createObjectURL(blob);
        imageUrls.set(cameraId, url);
        img.src = url;
    };
    
    ws.onerror = console.error;
    
    ws.onclose = function() {
        imageUrls.forEach(URL.revokeObjectURL);
        imageUrls.clear();
        setTimeout(() => location.reload(), 5000);
    };
})();
</script>
</body>
</html>
)rawliteral";

/**
 * Structure to manage connected camera clients
 * Stores client connection, ID, and activity timestamp
 */
struct CameraClient {
    WebsocketsClient client;
    int cameraId;
    unsigned long lastActivity;
    
    CameraClient(WebsocketsClient&& c, int id = -1) 
        : client(std::move(c))
        , cameraId(id)
        , lastActivity(millis()) {}
};

// Array to store camera client connections
std::array<std::unique_ptr<CameraClient>, MAX_CLIENTS> camClients;
size_t activeClients = 0;

/**
 * Adds a new camera client to the system
 * @param client WebSocket client to add
 * @return true if client was added successfully
 */
bool addCameraClient(WebsocketsClient&& client) {
    // Check system capacity
    if (activeClients >= MAX_CLIENTS) return false;
    
    // Verify sufficient memory
    if (ESP.getFreeHeap() < MAX_FRAME_SIZE + 1024) {
        return false;
    }
    
    // Find empty slot for new client
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        if (!camClients[i]) {
            camClients[i] = std::make_unique<CameraClient>(std::move(client), -1);
            ++activeClients;
            return true;
        }
    }
    return false;
}

/**
 * Removes a camera client and notifies browser clients
 * @param index Index of client to remove
 */
void removeCameraClient(size_t index) {
    if (index < MAX_CLIENTS && camClients[index]) {
        int removedId = camClients[index]->cameraId;
        // Notify browsers of disconnection
        String statusMsg = "{\"type\":\"status\",\"camera\":" + String(removedId) + ",\"status\":\"disconnected\"}";
        wsBrowser.textAll(statusMsg);
        
        camClients[index].reset();
        --activeClients;
    }
}

/**
 * Monitors system memory and performs cleanup if necessary
 * Runs periodic checks to prevent memory exhaustion
 */
void monitorMemory() {
    static unsigned long lastCheck = 0;
    static int lowMemCount = 0;
    const unsigned long now = millis();
    
    if (now - lastCheck > 1000) { // Check every second
        int freeHeap = ESP.getFreeHeap();
        lastCheck = now;
        
        // Handle low memory condition
        if (freeHeap < 10000) {
            lowMemCount++;
            
            // Force cleanup after sustained low memory
            if (lowMemCount > 5) {
                for (size_t i = 0; i < MAX_CLIENTS; ++i) {
                    if (camClients[i]) {
                        camClients[i].reset();
                        --activeClients;
                    }
                }
                lowMemCount = 0;
            }
        } else {
            lowMemCount = 0;
        }
    }
}

/**
 * Handles incoming WebSocket messages from camera clients
 * Processes both binary (video frames) and text (motion events) messages
 * @param client Reference to camera client
 * @return true if message was processed successfully
 */
bool handleWebSocketMessage(CameraClient& client) {
    if (!client.client.available()) return false;

    if (ESP.getFreeHeap() < MAX_FRAME_SIZE + 1024) {
        return false;
    }

    WebsocketsMessage msg = client.client.readBlocking();
    
    if (msg.isText()) {
        if (msg.data().indexOf("\"type\":\"motion\"") >= 0) {
            DynamicJsonDocument doc(256);
            deserializeJson(doc, msg.data());
            doc["camera"] = client.cameraId;
            String modifiedMsg;
            serializeJson(doc, modifiedMsg);
            wsBrowser.textAll(modifiedMsg);
        }
        return true;
    } else if (msg.isBinary()) {
        size_t msgLen = msg.length();
        if (msgLen > MAX_FRAME_SIZE) {
            return false;
        }

        if (msgLen <= StaticFrameBuffer::SIZE) {
            memcpy(frameBuffer.data, msg.c_str(), msgLen);
            frameBuffer.length = msgLen;

            if (frameBuffer.length > 12) {
                uint8_t receivedCameraId = frameBuffer.data[12];
                if (receivedCameraId == 0x01 || receivedCameraId == 0x02) {
                    if (client.cameraId != receivedCameraId) {
                        String statusMsg = "{\"type\":\"status\",\"camera\":" + String(client.cameraId) + ",\"status\":\"disconnected\"}";
                        wsBrowser.textAll(statusMsg);
                        
                        client.cameraId = receivedCameraId;
                        
                        statusMsg = "{\"type\":\"status\",\"camera\":" + String(client.cameraId) +  ",\"status\":\"connected\"}";
                        wsBrowser.textAll(statusMsg);
                    }
                }
            }
            
            constexpr size_t CHUNK_SIZE = 8192;
            for (size_t offset = 0; offset < frameBuffer.length; offset += CHUNK_SIZE) {
                size_t chunk = min(CHUNK_SIZE, frameBuffer.length - offset);
                wsBrowser.binaryAll(frameBuffer.data + offset, chunk);
                delay(1);
            }
            return true;
        }
    }
    return false;
}

/**
 * System initialization
 * Sets up WiFi AP, web server, and WebSocket servers
 */
void setup() {
    Serial.begin(115200);
    WiFi.softAP(ssid, password);
    IPAddress IP = WiFi.softAPIP();

    wsBrowser.onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {

    IPAddress clientIP;
    
    switch (type) {
        case WS_EVT_CONNECT:
            clientIP = client->remoteIP();
            Serial.printf("Browser connected: %s\n", clientIP.toString().c_str());
            break;
        case WS_EVT_DISCONNECT:
            clientIP = client->remoteIP();
            Serial.printf("Browser disconnected: %s\n", clientIP.toString().c_str());
            break;
        case WS_EVT_ERROR:
            Serial.printf("Error #%u: %s\n", client->id(), (char*)data);
            break;
        default:
            break;
    }
    });

    server.addHandler(&wsBrowser);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", index_html);
    });

    server.begin();
    wsServer.listen(8888);
}

/**
 * Main program loop
 * Handles client connections, messages, and system monitoring
 */
void loop() {
    static unsigned long lastMemCheck = 0;
    const unsigned long currentTime = millis();
    
    monitorMemory();

    if (currentTime - lastMemCheck > 5000) {
        lastMemCheck = currentTime;
    }

    if (wsServer.poll() && activeClients < MAX_CLIENTS) {
        auto newClient = wsServer.accept();
        if (newClient.available()) {
            if (addCameraClient(std::move(newClient))) {
            }
        }
    }

    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        if (camClients[i]) {
            auto& client = *camClients[i];
            
            if (handleWebSocketMessage(client)) {
                client.lastActivity = currentTime;
            } else if (currentTime - client.lastActivity > CLIENT_TIMEOUT) {
                removeCameraClient(i);
            }
        }
    }

    delay(10);
}