#include "HTTPServerController.h"
#include <ArduinoJson.h>
#include <FreeRTOS.h>
#include <task.h>
#include "RuntimeLog.h"

static const char* TAG = "HTTP";

// HTML template stored in PROGMEM to avoid RAM usage and blocking string concatenation
const char html_template[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>KineMachina Turntable Controller</title>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
        * {
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0;
            padding: 0;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 50%, #0f3460 100%);
            color: #e0e0e0;
            min-height: 100vh;
        }
        
        .header {
            background: linear-gradient(135deg, #e94560 0%, #c73650 100%);
            color: #fff;
            padding: 20px;
            text-align: center;
            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
            border-bottom: 3px solid #ff6b6b;
        }
        
        .header h1 {
            margin: 0;
            font-size: 28px;
            font-weight: bold;
            text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.5);
            letter-spacing: 1px;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
        }
        
        .status {
            padding: 15px;
            background: rgba(30, 30, 46, 0.8);
            margin: 15px 0;
            border-radius: 8px;
            border: 1px solid rgba(233, 69, 96, 0.3);
            box-shadow: 0 2px 8px rgba(0, 0, 0, 0.3);
        }
        
        h2 {
            margin-top: 30px;
            border-top: 2px solid rgba(233, 69, 96, 0.5);
            padding-top: 15px;
            color: #ff6b6b;
            font-size: 22px;
        }
        
        h3 {
            color: #ffa500;
            margin-top: 20px;
            font-size: 18px;
            border-left: 4px solid #ffa500;
            padding-left: 10px;
        }
        
        .status-item {
            margin: 8px 0;
            padding: 5px 0;
            border-bottom: 1px solid rgba(255, 255, 255, 0.1);
        }
        
        .status-item:last-child {
            border-bottom: none;
        }
        
        .status-item strong {
            color: #ffa500;
        }
        
        button {
            padding: 12px 24px;
            margin: 5px;
            font-size: 16px;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            transition: all 0.3s ease;
            font-weight: 600;
            box-shadow: 0 2px 4px rgba(0, 0, 0, 0.3);
        }
        
        button:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.4);
        }
        
        button:active {
            transform: translateY(0);
        }
        
        input {
            padding: 10px;
            margin: 5px;
            width: 300px;
            border: 2px solid rgba(233, 69, 96, 0.5);
            border-radius: 6px;
            background: rgba(30, 30, 46, 0.8);
            color: #e0e0e0;
            font-size: 14px;
        }
        
        input:focus {
            outline: none;
            border-color: #e94560;
            box-shadow: 0 0 8px rgba(233, 69, 96, 0.5);
        }
        
        .loading {
            color: #888;
            font-style: italic;
        }
        
        p {
            color: #b0b0b0;
            line-height: 1.6;
        }
        
        p small {
            color: #888;
            font-size: 12px;
        }
        
        #fullStatus {
            background: rgba(15, 52, 96, 0.6);
            border: 1px solid rgba(233, 69, 96, 0.4);
        }
        
        #danceButtons {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            margin: 10px 0;
        }
        
        #danceButtons button {
            flex: 1 1 auto;
            min-width: 120px;
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>KineMachina Turntable Controller</h1>
    </div>
    <div class="container">
    <div class='status' id='status'>
        <strong>Status:</strong>
        <div class='status-item'><strong>Free Heap:</strong> <span id='freeHeap' class='loading'>Loading...</span> bytes</div>
        <div class='status-item'><strong>Stepper Position:</strong> <span id='stepperPosition' class='loading'>Loading...</span>&deg;</div>
        <div class='status-item'><strong>Stepper Enabled:</strong> <span id='stepperEnabled' class='loading'>Loading...</span></div>
        <div class='status-item'><strong>Behavior Running:</strong> <span id='behaviorStatus' class='loading'>Loading...</span></div>
        <button onclick='loadFullStatus()' style='background-color:#3498db;color:white;margin-top:10px;'>Show Full Status (TMC2209)</button>
        <div id='fullStatus' style='margin-top:10px;display:none;padding:10px;background:#f9f9f9;border:1px solid #ddd;border-radius:4px;'></div>
    </div>
    
    <h2>Stepper Motor Control</h2>
    <script>
        // REST API helper functions
        function sendJson(url, data, callback) {
            const body = JSON.stringify(data);
            console.log('Sending to', url, 'with body:', body);
            fetch(url, {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: body
            }).then(r => {
                if (!r.ok) {
                    return r.json().then(err => { throw new Error(err.message || 'Request failed'); });
                }
                return r.json();
            }).then(data => {
                console.log('Response:', data);
                if (callback) callback(data);
                else {
                    updateStatus(); // Refresh status after action
                }
            }).catch(err => {
                console.error('Error:', err);
                alert('Error: ' + err.message);
            });
        }
        
        function getJson(url, callback) {
            fetch(url)
                .then(r => r.json())
                .then(data => {
                    if (callback) callback(data);
                })
                .catch(err => {
                    console.error('Error fetching', url, ':', err);
                });
        }
        
        // Update status from REST API
        function updateStatus() {
            // Get system status
            getJson('/status', function(data) {
                if (data.freeHeap !== undefined) {
                    document.getElementById('freeHeap').textContent = data.freeHeap;
                    document.getElementById('freeHeap').classList.remove('loading');
                }
            });
            
            // Get stepper status
            getJson('/stepper/status', function(data) {
                if (data.status === 'ok') {
                    if (data.stepperPosition !== undefined) {
                        document.getElementById('stepperPosition').textContent = (data.stepperPosition * 360.0 / (200 * (data.microsteps || 8)) / (data.gearRatio || 1)).toFixed(2);
                        document.getElementById('stepperPosition').classList.remove('loading');
                    }
                    if (data.enabled !== undefined) {
                        document.getElementById('stepperEnabled').textContent = data.enabled ? 'Yes' : 'No';
                        document.getElementById('stepperEnabled').classList.remove('loading');
                    }
                    if (data.behaviorInProgress !== undefined) {
                        document.getElementById('behaviorStatus').textContent = data.behaviorInProgress ? 'Yes' : 'No';
                        document.getElementById('behaviorStatus').classList.remove('loading');
                    }

                    // Update gear ratio in Motor & TMC section
                    if (data.gearRatio !== undefined) {
                        const gearInput = document.getElementById('motorGearRatio');
                        if (gearInput) {
                            gearInput.value = data.gearRatio;
                        }
                    }
                }
            });
        }
        
        // Load and display full status including TMC2209
        function loadFullStatus() {
            const fullStatusDiv = document.getElementById('fullStatus');
            fullStatusDiv.style.display = 'block';
            fullStatusDiv.innerHTML = '<div class="status-item">Loading full status...</div>';
            
            getJson('/stepper/status?full=true', function(data) {
                if (data.status === 'ok') {
                    let html = '<div class="status-item"><strong>Full Status (TMC2209)</strong></div>';
                    
                    // TMC2209 settings
                    if (data.tmc2209) {
                        const tmc = data.tmc2209;
                        html += '<div style="margin-left:20px;margin-top:10px;">';
                        html += '<div class="status-item"><strong>TMC2209 Settings:</strong></div>';
                        if (tmc.rmsCurrent !== undefined) {
                            html += '<div class="status-item">RMS Current: ' + tmc.rmsCurrent + ' mA</div>';
                        }
                        if (tmc.actualCurrent !== undefined) {
                            html += '<div class="status-item">Actual Current: ' + Math.round(tmc.actualCurrent) + ' mA';
                            if (tmc.csActual !== undefined) {
                                html += ' (CS: ' + tmc.csActual + ')';
                            }
                            html += '</div>';
                        }
                        if (tmc.irun !== undefined) {
                            const percent = Math.round((tmc.irun * 100) / 31);
                            html += '<div class="status-item">Running Current (irun): ' + tmc.irun + ' (' + percent + '%)</div>';
                        }
                        if (tmc.ihold !== undefined) {
                            const percent = Math.round((tmc.ihold * 100) / 31);
                            html += '<div class="status-item">Holding Current (ihold): ' + tmc.ihold + ' (' + percent + '%)</div>';
                        }
                        if (tmc.enabled !== undefined) {
                            html += '<div class="status-item">Driver Enabled: ' + (tmc.enabled ? 'Yes' : 'No') + '</div>';
                        }
                        if (tmc.spreadCycle !== undefined) {
                            html += '<div class="status-item">Mode: ' + (tmc.spreadCycle ? 'SpreadCycle' : 'StealthChop') + '</div>';
                        }
                        if (tmc.pwmAutoscale !== undefined) {
                            html += '<div class="status-item">PWM Autoscale: ' + (tmc.pwmAutoscale ? 'Enabled' : 'Disabled') + '</div>';
                        }
                        if (tmc.blankTime !== undefined) {
                            html += '<div class="status-item">Blank Time: ' + tmc.blankTime + '</div>';
                        }
                        html += '</div>';
                    }
                    
                    fullStatusDiv.innerHTML = html;
                } else {
                    fullStatusDiv.innerHTML = '<div class="status-item" style="color:red;">Error loading full status</div>';
                }
            });
        }
        
        // Load available dances and create buttons dynamically
        function loadDances() {
            getJson('/stepper/dance', function(data) {
                if (data.status === 'ok' && data.dances) {
                    const danceContainer = document.getElementById('danceButtons');
                    danceContainer.innerHTML = ''; // Clear existing buttons
                    
                    // Generic color palette (cycles through)
                    const colorPalette = ['#9b59b6', '#e74c3c', '#3498db', '#f39c12', '#1abc9c', '#e67e22', '#95a5a6', '#34495e'];
                    
                    data.dances.forEach(function(dance, index) {
                        const button = document.createElement('button');
                        button.textContent = dance.name;
                        // Use color from palette based on index (cycles if more dances than colors)
                        button.style.backgroundColor = colorPalette[index % colorPalette.length];
                        button.style.color = 'white';
                        button.onclick = function() {
                            sendJson('/stepper/dance', {danceType: dance.id});
                        };
                        button.title = dance.description; // Tooltip
                        danceContainer.appendChild(button);
                    });
                }
            });
        }

        // Load available behaviors and create buttons dynamically
        function loadBehaviors() {
            getJson('/stepper/behavior', function(data) {
                if (data.status === 'ok' && data.behaviors) {
                    const behaviorContainer = document.getElementById('behaviorButtons');
                    behaviorContainer.innerHTML = '';

                    const colorPalette = ['#e94560', '#ffa500', '#4ecca3', '#9b59b6', '#e67e22', '#1abc9c', '#f1c40f', '#3498db'];

                    data.behaviors.forEach(function(behavior, index) {
                        const button = document.createElement('button');
                        button.textContent = behavior.name;
                        button.style.backgroundColor = colorPalette[index % colorPalette.length];
                        button.style.color = 'white';
                        button.onclick = function() {
                            sendJson('/stepper/behavior', {behaviorType: behavior.id});
                        };
                        button.title = behavior.description;
                        behaviorContainer.appendChild(button);
                    });
                }
            });
        }
        
        // MQTT Configuration functions
        function loadMqttConfig() {
            getJson('/mqtt/config', function(data) {
                if (data.status === 'ok') {
                    document.getElementById('mqttEnabled').checked = data.enabled || false;
                    document.getElementById('mqttBroker').value = data.broker || '';
                    document.getElementById('mqttPort').value = data.port || 1883;
                    document.getElementById('mqttUsername').value = data.username || '';
                    document.getElementById('mqttPassword').value = data.password || '';
                    document.getElementById('mqttDeviceId').value = data.deviceId || '';
                    document.getElementById('mqttBaseTopic').value = data.baseTopic || '';
                    document.getElementById('mqttQosCommands').value = data.qosCommands !== undefined ? data.qosCommands : 1;
                    document.getElementById('mqttQosStatus').value = data.qosStatus !== undefined ? data.qosStatus : 0;
                    document.getElementById('mqttKeepalive').value = data.keepalive || 60;
                    
                    // Update status display
                    const statusText = data.enabled ? 'Enabled' : 'Disabled';
                    document.getElementById('mqttStatus').textContent = statusText;
                    document.getElementById('mqttStatus').classList.remove('loading');
                }
            });
        }
        
        function saveMqttConfig() {
            const config = {
                enabled: document.getElementById('mqttEnabled').checked,
                broker: document.getElementById('mqttBroker').value,
                port: parseInt(document.getElementById('mqttPort').value) || 1883,
                username: document.getElementById('mqttUsername').value,
                password: document.getElementById('mqttPassword').value,
                deviceId: document.getElementById('mqttDeviceId').value,
                baseTopic: document.getElementById('mqttBaseTopic').value,
                qosCommands: parseInt(document.getElementById('mqttQosCommands').value) || 1,
                qosStatus: parseInt(document.getElementById('mqttQosStatus').value) || 0,
                keepalive: parseInt(document.getElementById('mqttKeepalive').value) || 60
            };
            
            sendJson('/mqtt/config', config, function(data) {
                if (data.status === 'ok') {
                    alert('MQTT configuration saved successfully!');
                    loadMqttConfig(); // Reload to show updated status
                }
            });
        }
        
        // Motor & TMC Configuration
        function loadMotorConfig() {
            getJson('/motor/config', function(data) {
                if (data.status === 'ok') {
                    if (data.motorMaxSpeed !== undefined) document.getElementById('motorMaxSpeed').value = data.motorMaxSpeed;
                    if (data.motorAcceleration !== undefined) document.getElementById('motorAcceleration').value = data.motorAcceleration;
                    if (data.motorMicrosteps !== undefined) document.getElementById('motorMicrosteps').value = data.motorMicrosteps;
                    if (data.motorGearRatio !== undefined) document.getElementById('motorGearRatio').value = data.motorGearRatio;
                    if (data.tmcRmsCurrent !== undefined) document.getElementById('tmcRmsCurrent').value = data.tmcRmsCurrent;
                    if (data.tmcIrun !== undefined) document.getElementById('tmcIrun').value = data.tmcIrun;
                    if (data.tmcIhold !== undefined) document.getElementById('tmcIhold').value = data.tmcIhold;
                    if (data.tmcSpreadCycle !== undefined) document.getElementById('tmcSpreadCycle').checked = data.tmcSpreadCycle;
                    if (data.tmcPwmAutoscale !== undefined) document.getElementById('tmcPwmAutoscale').checked = data.tmcPwmAutoscale;
                    if (data.tmcBlankTime !== undefined) document.getElementById('tmcBlankTime').value = data.tmcBlankTime;
                }
            });
        }
        
        function saveMotorConfig() {
            const config = {
                motorMaxSpeed: parseFloat(document.getElementById('motorMaxSpeed').value) || 2000,
                motorAcceleration: parseFloat(document.getElementById('motorAcceleration').value) || 400,
                motorMicrosteps: parseInt(document.getElementById('motorMicrosteps').value) || 8,
                motorGearRatio: parseFloat(document.getElementById('motorGearRatio').value) || 1,
                tmcRmsCurrent: parseInt(document.getElementById('tmcRmsCurrent').value) || 1200,
                tmcIrun: Math.min(31, Math.max(0, parseInt(document.getElementById('tmcIrun').value) || 31)),
                tmcIhold: Math.min(31, Math.max(0, parseInt(document.getElementById('tmcIhold').value) || 31)),
                tmcSpreadCycle: document.getElementById('tmcSpreadCycle').checked,
                tmcPwmAutoscale: document.getElementById('tmcPwmAutoscale').checked,
                tmcBlankTime: Math.min(24, Math.max(0, parseInt(document.getElementById('tmcBlankTime').value) || 24))
            };
            sendJson('/motor/config', config, function(data) {
                if (data.status === 'ok') {
                    alert(data.message || 'Motor and TMC configuration saved and applied.');
                    loadMotorConfig();
                    updateStatus();
                }
            });
        }
        
        // Update status on page load and then every 2 seconds
        updateStatus();
        loadDances(); // Load dances on page load
        loadBehaviors(); // Load behaviors on page load
        loadMotorConfig(); // Load motor/TMC config on page load
        loadMqttConfig(); // Load MQTT config on page load
        setInterval(updateStatus, 2000);
    </script>
    
    <h3>Position Control</h3>
    <input type='number' id='position' placeholder='Target position (degrees)' value='0' step='0.1'>
    <button onclick='sendJson("/stepper/position", {position: parseFloat(document.getElementById("position").value)})'>Move To Position</button>
    <button onclick='sendJson("/stepper/zero", {})'>Zero Position</button>
    <button onclick='sendJson("/stepper/stopMove", {})' style='background-color:#ff6b6b;color:white;'>Stop Move</button>
    
    <h3>Heading Control (Shortest Path)</h3>
    <p><small>Move to a heading using the shortest path. Uses stepper position to determine current heading and computes the relative shortest angle.</small></p>
    <input type='number' id='heading' placeholder='Target heading (0-360 degrees)' value='0' min='0' max='360' step='0.1'>
    <button onclick='sendJson("/stepper/heading", {heading: parseFloat(document.getElementById("heading").value)})' style='background-color:#9b59b6;color:white;'>Move To Heading</button>
    
    <h3>Homing</h3>
    <p><small>Home the motor to position zero.</small></p>
    <button onclick='sendJson("/stepper/home", {home: true})' style='background-color:#4CAF50;color:white;'>Home Now</button>
    
    <h3>Velocity Control (Continuous Rotation)</h3>
    <button onclick='sendJson("/stepper/runForward", {})' style='background-color:#4CAF50;color:white;'>Run Forward</button>
    <button onclick='sendJson("/stepper/runBackward", {})' style='background-color:#2196F3;color:white;'>Run Backward</button>
    <button onclick='sendJson("/stepper/stopMove", {})' style='background-color:#ff9800;color:white;'>Stop</button>
    <button onclick='sendJson("/stepper/forceStop", {})' style='background-color:#ff6b6b;color:white;'>Force Stop</button>
    
    <h3>Motor Control</h3>
    <button onclick='sendJson("/stepper/enable", {enable: true})'>Enable Motor</button>
    <button onclick='sendJson("/stepper/enable", {enable: false})'>Disable Motor</button>
    <button onclick='sendJson("/stepper/reset", {})' style='background-color:#ff6b6b;color:white;'>Reset Engine</button>
    <p><small>Reset FastAccelStepper engine (calls engine.init()). Use if motor control becomes unresponsive.</small></p>
    
    <h3>Dance Effects</h3>
    <p><small>Perform dance sequences with rotation patterns.</small></p>
    <div id='danceButtons'>
        <span class='loading'>Loading dances...</span>
    </div>
    <button onclick='sendJson("/stepper/stopDance", {})' style='background-color:#ff6b6b;color:white;margin-top:10px;'>Stop Dance</button>

    <h3>Behaviors</h3>
    <p><small>Behavioral presets using rotation-only motions.</small></p>
    <div id='behaviorButtons'>
        <span class='loading'>Loading behaviors...</span>
    </div>
    <button onclick='sendJson("/stepper/stopBehavior", {})' style='background-color:#ff6b6b;color:white;margin-top:10px;'>Stop Behavior</button>
    
    <h2>Motor & TMC Settings</h2>
    <p><small>Stepper and TMC2209 driver settings. Saved to NVS and applied immediately.</small></p>
    
    <h3>Motor</h3>
    <div style='margin-bottom: 10px;'>
        <label><strong>Max Speed (steps/sec):</strong></label><br>
        <input type='number' id='motorMaxSpeed' min='1' max='10000' step='10' style='width: 100%; max-width: 200px;'>
    </div>
    <div style='margin-bottom: 10px;'>
        <label><strong>Acceleration (steps/sec²):</strong></label><br>
        <input type='number' id='motorAcceleration' min='1' max='10000' step='10' style='width: 100%; max-width: 200px;'>
    </div>
    <div style='margin-bottom: 10px;'>
        <label><strong>Microsteps:</strong></label><br>
        <select id='motorMicrosteps' style='width: 100%; max-width: 200px; padding: 10px; background: rgba(30,30,46,0.8); color: #e0e0e0; border: 2px solid rgba(233,69,96,0.5); border-radius: 6px;'>
            <option value='1'>1</option><option value='2'>2</option><option value='4'>4</option><option value='8'>8</option>
            <option value='16'>16</option><option value='32'>32</option><option value='64'>64</option><option value='128'>128</option><option value='256'>256</option>
        </select>
    </div>
    <div style='margin-bottom: 10px;'>
        <label><strong>Gear Ratio (stepper:turntable):</strong></label><br>
        <input type='number' id='motorGearRatio' min='0.1' max='100' step='0.1' style='width: 100%; max-width: 200px;'>
    </div>
    <h3>TMC2209 Driver</h3>
    <div style='margin-bottom: 10px;'>
        <label><strong>RMS Current (mA):</strong></label><br>
        <input type='number' id='tmcRmsCurrent' min='0' max='2000' step='50' style='width: 100%; max-width: 200px;'>
    </div>
    <div style='margin-bottom: 10px;'>
        <label><strong>IRUN (0-31, run current scale):</strong></label><br>
        <input type='number' id='tmcIrun' min='0' max='31' style='width: 100%; max-width: 200px;'>
    </div>
    <div style='margin-bottom: 10px;'>
        <label><strong>IHOLD (0-31, hold current scale):</strong></label><br>
        <input type='number' id='tmcIhold' min='0' max='31' style='width: 100%; max-width: 200px;'>
    </div>
    <div style='margin-bottom: 10px;'>
        <label style='display: flex; align-items: center;'>
            <input type='checkbox' id='tmcSpreadCycle' style='width: auto; margin-right: 10px;'>
            <strong>SpreadCycle</strong> (off = StealthChop)
        </label>
    </div>
    <div style='margin-bottom: 10px;'>
        <label style='display: flex; align-items: center;'>
            <input type='checkbox' id='tmcPwmAutoscale' style='width: auto; margin-right: 10px;'>
            <strong>PWM Autoscale</strong>
        </label>
    </div>
    <div style='margin-bottom: 10px;'>
        <label><strong>Blank Time (0-24):</strong></label><br>
        <input type='number' id='tmcBlankTime' min='0' max='24' style='width: 100%; max-width: 200px;'>
    </div>
    
    <button onclick='saveMotorConfig()' style='background-color:#4CAF50;color:white;margin-top:10px;'>Save Motor & TMC Configuration</button>
    <button onclick='loadMotorConfig()' style='background-color:#3498db;color:white;margin-top:10px;'>Reload Settings</button>
    
    <h2>MQTT Configuration</h2>
    <div class='status' id='mqttConfigStatus'>
        <div class='status-item'><strong>MQTT Status:</strong> <span id='mqttStatus' class='loading'>Loading...</span></div>
    </div>
    
    <h3>MQTT Settings</h3>
    <p><small>Configure MQTT broker connection settings. Changes are saved to EEPROM.</small></p>
    
    <div style='margin-bottom: 10px;'>
        <label style='display: flex; align-items: center; margin-bottom: 10px;'>
            <input type='checkbox' id='mqttEnabled' style='width: auto; margin-right: 10px;'>
            <strong>Enable MQTT</strong>
        </label>
    </div>
    
    <div style='margin-bottom: 10px;'>
        <label><strong>Broker:</strong></label><br>
        <input type='text' id='mqttBroker' placeholder='mqtt.broker.local' style='width: 100%; max-width: 400px;'>
    </div>
    
    <div style='margin-bottom: 10px;'>
        <label><strong>Port:</strong></label><br>
        <input type='number' id='mqttPort' placeholder='1883' min='1' max='65535' style='width: 100%; max-width: 400px;'>
    </div>
    
    <div style='margin-bottom: 10px;'>
        <label><strong>Username:</strong></label><br>
        <input type='text' id='mqttUsername' placeholder='(optional)' style='width: 100%; max-width: 400px;'>
    </div>
    
    <div style='margin-bottom: 10px;'>
        <label><strong>Password:</strong></label><br>
        <input type='password' id='mqttPassword' placeholder='(optional)' style='width: 100%; max-width: 400px;'>
    </div>
    
    <div style='margin-bottom: 10px;'>
        <label><strong>Device ID:</strong></label><br>
        <input type='text' id='mqttDeviceId' placeholder='turntable_001' style='width: 100%; max-width: 400px;'>
    </div>
    
    <div style='margin-bottom: 10px;'>
        <label><strong>Base Topic:</strong></label><br>
        <input type='text' id='mqttBaseTopic' placeholder='kinemachina/turntable' style='width: 100%; max-width: 400px;'>
    </div>
    
    <div style='margin-bottom: 10px;'>
        <label><strong>QoS Commands:</strong></label><br>
        <input type='number' id='mqttQosCommands' placeholder='1' min='0' max='2' style='width: 100%; max-width: 400px;'>
        <p><small>Quality of Service for command topics (0-2). Default: 1</small></p>
    </div>
    
    <div style='margin-bottom: 10px;'>
        <label><strong>QoS Status:</strong></label><br>
        <input type='number' id='mqttQosStatus' placeholder='0' min='0' max='2' style='width: 100%; max-width: 400px;'>
        <p><small>Quality of Service for status topics (0-2). Default: 0</small></p>
    </div>
    
    <div style='margin-bottom: 10px;'>
        <label><strong>Keepalive (seconds):</strong></label><br>
        <input type='number' id='mqttKeepalive' placeholder='60' min='10' max='65535' style='width: 100%; max-width: 400px;'>
    </div>
    
    <button onclick='saveMqttConfig()' style='background-color:#4CAF50;color:white;margin-top:10px;'>Save MQTT Configuration</button>
    <button onclick='loadMqttConfig()' style='background-color:#3498db;color:white;margin-top:10px;'>Reload Settings</button>
    </div>
</body>
</html>
)rawliteral";

HTTPServerController::HTTPServerController(const char *ssid, const char *password, int port)
    : wifiSSID(ssid), wifiPassword(password), httpPort(port),
      server(nullptr), stepperController(nullptr), commandQueue(nullptr), configManager(nullptr), mqttController(nullptr)
{
}

HTTPServerController::~HTTPServerController()
{
    if (server)
    {
        delete server;
    }
}

bool HTTPServerController::initWiFi()
{
    ESP_LOGI(TAG, "Initializing WiFi...");
    ESP_LOGI(TAG, "Connecting to: %s", wifiSSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPassword);

    // Wait for connection with timeout
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30)
    {
        delay(500);
        ESP_LOGD(TAG, ".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        ESP_LOGI(TAG, "WiFi connected!");
        ESP_LOGI(TAG, "IP address: %s", WiFi.localIP().toString().c_str());
        return true;
    }
    else
    {
        ESP_LOGE(TAG, "WiFi connection failed!");
        return false;
    }
}

void HTTPServerController::logRequest(AsyncWebServerRequest *request, const char *endpoint)
{
    if (!request)
        return;

    // Build method string
    const char* method = request->method() == HTTP_GET ? "GET" : request->method() == HTTP_POST ? "POST"
                                                     : request->method() == HTTP_PUT    ? "PUT"
                                                     : request->method() == HTTP_DELETE ? "DELETE"
                                                                                        : "UNKNOWN";

    // Build query parameters string
    String params;
    int argCount = request->args();
    if (argCount > 0)
    {
        params = " [";
        for (int i = 0; i < argCount; i++)
        {
            if (i > 0)
                params += ", ";
            params += request->argName(i);
            params += "=";
            params += request->arg(i);
        }
        params += "]";
    }

    // Build body string for POST requests
    String bodyStr;
    if (request->method() == HTTP_POST)
    {
        String body;
        if (request->hasParam("plain", true))
        {
            body = request->getParam("plain", true)->value();
        }
        else if (request->_tempObject != nullptr)
        {
            String *bodyPtr = (String *)request->_tempObject;
            body = *bodyPtr;
        }

        if (body.length() > 0)
        {
            bodyStr = " Body: " + body;
        }
    }

    ESP_LOGI(TAG, "%s %s from %s%s%s", method, endpoint,
             request->client()->remoteIP().toString().c_str(),
             params.c_str(), bodyStr.c_str());
}

bool HTTPServerController::getJsonBody(AsyncWebServerRequest *request, String &body, String &errorMsg)
{
    // Check Content-Type header
    if (!request->hasHeader("Content-Type"))
    {
        errorMsg = "Missing Content-Type header";
        return false;
    }

    String contentType = request->header("Content-Type");
    if (contentType.length() == 0 || contentType.indexOf("application/json") == -1)
    {
        errorMsg = "Invalid Content-Type. Expected 'application/json', got '" + contentType + "'";
        return false;
    }

    // Extract body from _tempObject (set by body handler)
    if (request->_tempObject != nullptr)
    {
        String *bodyPtr = (String *)request->_tempObject;
        body = *bodyPtr;
        // Clean up the temporary object
        delete bodyPtr;
        request->_tempObject = nullptr;
    }
    else
    {
        errorMsg = "Request body not found";
        return false;
    }

    // Check if body is empty
    if (body.length() == 0)
    {
        errorMsg = "Empty request body";
        return false;
    }

    return true;
}

void HTTPServerController::sendErrorResponse(AsyncWebServerRequest *request, int code, const String &message)
{
    ErrorResponse error;
    error.message = message;

    JsonDocument doc;
    doc["status"] = error.status;
    doc["message"] = error.message;
    String responseStr;
    serializeJson(doc, responseStr);
    request->send(code, "application/json", responseStr);
}

// Template processor function - replaces placeholders in HTML template
String HTTPServerController::processTemplate(const String &var)
{
    // Status values
    if (var == "FREE_HEAP")
    {
        return String(ESP.getFreeHeap());
    }
    if (var == "CURRENT_POS")
    {
        return String(stepperController->getStepperPositionDegrees(), 2);
    }
    if (var == "ENABLED")
    {
        return stepperController->isEnabled() ? "Yes" : "No";
    }
    // Velocity control
    if (var == "SPEED_HZ")
    {
        return String(stepperController->getTargetSpeedHz(), 2);
    }

    // Microstepping
    if (var == "MICROSTEPS")
    {
        return String(stepperController->getMicrosteps());
    }
    uint8_t currentMicrosteps = stepperController->getMicrosteps();
    if (var == "MICROSTEPS_1_SELECTED")
        return (currentMicrosteps == 1) ? "selected" : "";
    if (var == "MICROSTEPS_2_SELECTED")
        return (currentMicrosteps == 2) ? "selected" : "";
    if (var == "MICROSTEPS_4_SELECTED")
        return (currentMicrosteps == 4) ? "selected" : "";
    if (var == "MICROSTEPS_8_SELECTED")
        return (currentMicrosteps == 8) ? "selected" : "";
    if (var == "MICROSTEPS_16_SELECTED")
        return (currentMicrosteps == 16) ? "selected" : "";
    if (var == "MICROSTEPS_32_SELECTED")
        return (currentMicrosteps == 32) ? "selected" : "";
    if (var == "MICROSTEPS_64_SELECTED")
        return (currentMicrosteps == 64) ? "selected" : "";
    if (var == "MICROSTEPS_128_SELECTED")
        return (currentMicrosteps == 128) ? "selected" : "";
    if (var == "MICROSTEPS_256_SELECTED")
        return (currentMicrosteps == 256) ? "selected" : "";

    // Gear ratio
    if (var == "GEAR_RATIO")
    {
        return String(stepperController->getGearRatio(), 2);
    }
    
    // TMC2209 Settings
    if (var == "TMC_RMS_CURRENT")
    {
        return String(stepperController->getTmcRmsCurrent());
    }
    if (var == "TMC_CS_ACTUAL")
    {
        return String(stepperController->getTmcCsActual());
    }
    if (var == "TMC_ACTUAL_CURRENT")
    {
        return String(stepperController->getTmcActualCurrent(), 0);
    }
    if (var == "TMC_IRUN")
    {
        uint8_t irun = stepperController->getTmcIrun();
        uint8_t percent = (irun * 100) / 31;
        return String(irun) + " (" + String(percent) + "%)";
    }
    if (var == "TMC_IHOLD")
    {
        uint8_t ihold = stepperController->getTmcIhold();
        uint8_t percent = (ihold * 100) / 31;
        return String(ihold) + " (" + String(percent) + "%)";
    }
    if (var == "TMC_ENABLED")
    {
        return stepperController->getTmcEnabled() ? "Yes" : "No";
    }
    if (var == "TMC_MODE")
    {
        return stepperController->getTmcSpreadCycle() ? "SpreadCycle" : "StealthChop";
    }
    if (var == "TMC_PWM_AUTOSCALE")
    {
        return stepperController->getTmcPwmAutoscale() ? "Enabled" : "Disabled";
    }
    if (var == "TMC_BLANK_TIME")
    {
        return String(stepperController->getTmcBlankTime());
    }
    
    // Auto-home (removed feature, but keep template variable for compatibility)
    if (var == "AUTO_HOME")
    {
        return "Disabled";
    }

    return String(); // Return empty string for unknown variables
}

void HTTPServerController::handleRoot(AsyncWebServerRequest *request)
{
    logRequest(request, "/");

    // Send static HTML (no template processing needed - uses AJAX for data)
    request->send(200, "text/html", html_template);

    ESP_LOGI(TAG, "Response: 200 OK (HTML)");
}

void HTTPServerController::handleStatus(AsyncWebServerRequest *request)
{
    logRequest(request, "/status");

    JsonDocument doc;
    doc["status"] = "ok";
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["stepperEnabled"] = stepperController->isEnabled();

    doc["wifiStatus"] = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
    if (WiFi.status() == WL_CONNECTED)
    {
        doc["ipAddress"] = WiFi.localIP().toString();
    }

    String responseStr;
    serializeJson(doc, responseStr);
    request->send(200, "application/json", responseStr);
    ESP_LOGI(TAG, "Response: 200 OK");
}

void HTTPServerController::handleMoveTo(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/position");

    if (request->method() == HTTP_POST)
    {
        // Extract and validate JSON body
        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            ESP_LOGW(TAG, "Body validation error: %s", errorMsg.c_str());
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        // Parse JSON into struct
        PositionRequest req;
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        // Deserialize into struct
        req.position = doc["position"] | 0.0f;

        // Validate required field
        if (!doc["position"].is<float>())
        {
            sendErrorResponse(request, 400, "Missing or invalid 'position' parameter (float)");
            return;
        }

        ESP_LOGI(TAG, "Parsed values: position=%.3f", req.position);

        // Send command via queue
        if (commandQueue != nullptr)
        {
            MotorCommand cmd;
            cmd.type = MotorCommandType::MOVE_TO;
            cmd.data.position.value = req.position;
            cmd.statusCallback = nullptr;
            cmd.statusContext = nullptr;

            if (commandQueue->sendCommand(cmd, pdMS_TO_TICKS(100)))
            {
                JsonDocument response;
                response["status"] = "ok";
                response["message"] = "Target position set to " + String(req.position, 2) + "°";
                response["targetPositionDegrees"] = req.position;

                String responseStr;
                serializeJson(response, responseStr);
                request->send(200, "application/json", responseStr);
                ESP_LOGI(TAG, "Response: 200 OK");
            }
            else
            {
                sendErrorResponse(request, 503, "Command queue full");
                ESP_LOGW(TAG, "Response: 503 Service Unavailable - Queue full");
            }
        }
        else
        {
            // Fallback to direct call if queue not available
            stepperController->moveToDegrees(req.position);
            JsonDocument response;
            response["status"] = "ok";
            response["message"] = "Target position set to " + String(req.position, 2) + "°";
            response["targetPositionDegrees"] = req.position;
            response["currentPositionDegrees"] = stepperController->getStepperPositionDegrees();

            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
            ESP_LOGI(TAG, "Response: 200 OK");
        }
    }
    else if (request->method() == HTTP_GET)
    {
        // Get current and target position (in degrees)
        ESP_LOGI(TAG, "Getting current position");

        JsonDocument response;
        response["status"] = "ok";
        response["currentPositionDegrees"] = stepperController->getStepperPositionDegrees();
        response["targetPositionDegrees"] = 0.0f;

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperEnable(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/enable");

    if (request->method() == HTTP_POST)
    {
        // Extract and validate JSON body
        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            ESP_LOGW(TAG, "Body validation error: %s", errorMsg.c_str());
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        // Parse JSON
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        if (doc["enable"].is<bool>())
        {
            bool enable = doc["enable"].as<bool>();
            ESP_LOGI(TAG, "Parsed values: enable=%s", enable ? "true" : "false");

            // Send command via queue
            if (commandQueue != nullptr)
            {
                MotorCommand cmd;
                cmd.type = MotorCommandType::ENABLE;
                cmd.data.enable.enable = enable;
                cmd.statusCallback = nullptr;
                cmd.statusContext = nullptr;

                if (commandQueue->sendCommand(cmd, pdMS_TO_TICKS(100)))
                {
                    JsonDocument response;
                    response["status"] = "ok";
                    response["message"] = "Motor " + String(enable ? "enabled" : "disabled");
                    response["enabled"] = enable;

                    String responseStr;
                    serializeJson(response, responseStr);
                    request->send(200, "application/json", responseStr);
                    ESP_LOGI(TAG, "Response: 200 OK");
                }
                else
                {
                    sendErrorResponse(request, 503, "Command queue full");
                    ESP_LOGW(TAG, "Response: 503 Service Unavailable - Queue full");
                }
            }
            else
            {
                // Fallback to direct call
                stepperController->enable(enable);
                JsonDocument response;
                response["status"] = "ok";
                response["message"] = "Motor " + String(enable ? "enabled" : "disabled");
                response["enabled"] = enable;

                String responseStr;
                serializeJson(response, responseStr);
                request->send(200, "application/json", responseStr);
                ESP_LOGI(TAG, "Response: 200 OK");
            }
        }
        else
        {
            ESP_LOGW(TAG, "Response: 400 Bad Request - Missing enable parameter");
            sendErrorResponse(request, 400, "Missing enable parameter");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperSpeed(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/speed");

    if (request->method() == HTTP_POST)
    {
        // Extract and validate JSON body
        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            ESP_LOGW(TAG, "Body validation error: %s", errorMsg.c_str());
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        // Parse JSON into struct
        SpeedRequest req;
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        // Deserialize into struct
        req.speed = doc["speed"] | 0.0f;

        // Validate required field
        if (!doc["speed"].is<float>())
        {
            sendErrorResponse(request, 400, "Missing or invalid 'speed' parameter (float)");
            return;
        }

        ESP_LOGI(TAG, "Parsed values: speed=%.3f", req.speed);

        if (req.speed > 0)
        {
            stepperController->setMaxSpeed(req.speed);
            
            // Save configuration if configManager is available
            if (configManager != nullptr)
            {
                configManager->setMotorMaxSpeed(req.speed);
                configManager->save();
            }

            JsonDocument response;
            response["status"] = "ok";
            response["message"] = "Max speed set to " + String(req.speed) + " steps/sec";
            response["maxSpeed"] = req.speed;

            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
            ESP_LOGI(TAG, "Response: 200 OK");
        }
        else
        {
            sendErrorResponse(request, 400, "Speed must be greater than 0");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperAcceleration(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/acceleration");

    if (request->method() == HTTP_POST)
    {
        // Extract and validate JSON body
        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            ESP_LOGW(TAG, "Body validation error: %s", errorMsg.c_str());
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        // Parse JSON into struct
        AccelerationRequest req;
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        // Deserialize into struct
        req.accel = doc["accel"] | 0.0f;

        // Validate required field
        if (!doc["accel"].is<float>())
        {
            sendErrorResponse(request, 400, "Missing or invalid 'accel' parameter (float)");
            return;
        }

        ESP_LOGI(TAG, "Parsed values: accel=%.3f", req.accel);

        if (req.accel > 0)
        {
            stepperController->setAcceleration(req.accel);
            
            // Save configuration if configManager is available
            if (configManager != nullptr)
            {
                configManager->setMotorAcceleration(req.accel);
                configManager->save();
            }

            JsonDocument response;
            response["status"] = "ok";
            response["message"] = "Acceleration set to " + String(req.accel) + " steps/sec²";
            response["acceleration"] = req.accel;

            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
            ESP_LOGI(TAG, "Response: 200 OK");
        }
        else
        {
            sendErrorResponse(request, 400, "Acceleration must be greater than 0");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperMicrosteps(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/microsteps");

    if (request->method() == HTTP_POST)
    {
        // Extract and validate JSON body
        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            ESP_LOGW(TAG, "Body validation error: %s", errorMsg.c_str());
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        // Parse JSON into struct
        MicrostepsRequest req;
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        // Deserialize into struct
        req.microsteps = doc["microsteps"] | 1;

        // Validate required field
        if (!doc["microsteps"].is<int>())
        {
            sendErrorResponse(request, 400, "Missing or invalid 'microsteps' parameter (int)");
            return;
        }

        ESP_LOGI(TAG, "Parsed values: microsteps=%d", req.microsteps);

        int microsteps = req.microsteps;
        if (stepperController->setMicrosteps(microsteps))
        {
            ESP_LOGI(TAG, "Setting microstepping to: %d", microsteps);
            
            // Save configuration if configManager is available
            if (configManager != nullptr)
            {
                configManager->setMotorMicrosteps(microsteps);
                configManager->save();
            }

            JsonDocument response;
            response["status"] = "ok";
            response["message"] = "Microstepping set to " + String(microsteps);
            response["microsteps"] = microsteps;

            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
            ESP_LOGI(TAG, "Response: 200 OK");
        }
        else
        {
            ESP_LOGW(TAG, "Response: 400 Bad Request - Invalid microstepping value");
            sendErrorResponse(request, 400, "Invalid microstepping value. Must be 1, 2, 4, 8, 16, 32, 64, 128, or 256");
        }
    }
    else if (request->method() == HTTP_GET)
    {
        // Get current microstepping
        uint8_t microsteps = stepperController->getMicrosteps();

        JsonDocument response;
        response["status"] = "ok";
        response["microsteps"] = microsteps;

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperGearRatio(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/gearratio");

    if (request->method() == HTTP_POST)
    {
        // Extract and validate JSON body
        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            ESP_LOGW(TAG, "Body validation error: %s", errorMsg.c_str());
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        // Parse JSON into struct
        GearRatioRequest req;
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        // Deserialize into struct
        req.ratio = doc["ratio"] | 1.0f;

        // Validate required field
        if (!doc["ratio"].is<float>())
        {
            sendErrorResponse(request, 400, "Missing or invalid 'ratio' parameter (float)");
            return;
        }

        ESP_LOGI(TAG, "Parsed values: ratio=%.3f", req.ratio);

        float ratio = req.ratio;
        if (ratio > 0.0f && ratio <= 100.0f)
        {
            stepperController->setGearRatio(ratio);
            ESP_LOGI(TAG, "Setting gear ratio to: %.2f:1 (stepper:turntable)", ratio);
            
            // Save configuration if configManager is available
            if (configManager != nullptr)
            {
                configManager->setMotorGearRatio(ratio);
                configManager->save();
            }

            JsonDocument response;
            response["status"] = "ok";
            response["message"] = "Gear ratio set to " + String(ratio, 2) + ":1";
            response["gearRatio"] = ratio;

            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
            ESP_LOGI(TAG, "Response: 200 OK");
        }
        else
        {
            ESP_LOGW(TAG, "Response: 400 Bad Request - Invalid gear ratio value");
            sendErrorResponse(request, 400, "Invalid gear ratio value. Must be between 0.1 and 100.0");
        }
    }
    else if (request->method() == HTTP_GET)
    {
        // Get current gear ratio
        float ratio = stepperController->getGearRatio();

        JsonDocument response;
        response["status"] = "ok";
        response["gearRatio"] = ratio;

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperSpeedHz(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/speedHz");

    if (request->method() == HTTP_POST)
    {
        // Extract and validate JSON body
        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            ESP_LOGW(TAG, "Body validation error: %s", errorMsg.c_str());
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        // Parse JSON into struct
        SpeedHzRequest req;
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        // Deserialize into struct
        req.speedHz = doc["speedHz"] | 0.0f;

        // Validate required field
        if (!doc["speedHz"].is<float>())
        {
            sendErrorResponse(request, 400, "Missing or invalid 'speedHz' parameter (float, Hz = steps/sec)");
            return;
        }

        ESP_LOGI(TAG, "Parsed values: speedHz=%.3f", req.speedHz);

        if (req.speedHz < 0)
        {
            sendErrorResponse(request, 400, "Speed must be >= 0 Hz");
            return;
        }

        // Direct call (immediate execution)
        stepperController->setSpeedInHz(req.speedHz);

        JsonDocument response;
        response["status"] = "ok";
        response["message"] = "Speed set to " + String(req.speedHz, 2) + " Hz";
        response["speedHz"] = req.speedHz;

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else if (request->method() == HTTP_GET)
    {
        // Get current speed
        float speedHz = stepperController->getTargetSpeedHz();

        JsonDocument response;
        response["status"] = "ok";
        response["speedHz"] = speedHz;

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperRunForward(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/runForward");

    if (request->method() == HTTP_POST)
    {
        ESP_LOGI(TAG, "Starting forward rotation");

        // Direct call (immediate execution)
        stepperController->runForward();

        JsonDocument response;
        response["status"] = "ok";
        response["message"] = "Forward rotation started";
        response["direction"] = "forward";
        response["speedHz"] = stepperController->getTargetSpeedHz();

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperRunBackward(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/runBackward");

    if (request->method() == HTTP_POST)
    {
        ESP_LOGI(TAG, "Starting backward rotation");

        // Direct call (immediate execution)
        stepperController->runBackward();

        JsonDocument response;
        response["status"] = "ok";
        response["message"] = "Backward rotation started";
        response["direction"] = "backward";
        response["speedHz"] = stepperController->getTargetSpeedHz();

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperStopMove(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/stopMove");

    if (request->method() == HTTP_POST)
    {
        stepperController->stopMove();

        JsonDocument response;
        response["status"] = "ok";
        response["message"] = "Move stopped";

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperForceStop(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/forceStop");

    if (request->method() == HTTP_POST)
    {
        stepperController->stopVelocity();

        JsonDocument response;
        response["status"] = "ok";
        response["message"] = "Force stop executed";

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperReset(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/reset");

    if (request->method() == HTTP_POST)
    {
        ESP_LOGI(TAG, "Resetting FastAccelStepper engine...");

        // Call resetEngine directly (immediate execution, no queue needed)
        bool success = stepperController->resetEngine();

        JsonDocument response;
        if (success)
        {
            response["status"] = "ok";
            response["message"] = "FastAccelStepper engine reset successfully";
            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
            ESP_LOGI(TAG, "Response: 200 OK - Engine reset");
        }
        else
        {
            response["status"] = "error";
            response["message"] = "Failed to reset FastAccelStepper engine";
            String responseStr;
            serializeJson(response, responseStr);
            request->send(500, "application/json", responseStr);
            ESP_LOGE(TAG, "Response: 500 Internal Server Error - Reset failed");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperStatus(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/status");

    if (request->method() == HTTP_GET)
    {
        // Check if full status is requested via query parameter
        bool fullStatus = false;
        if (request->hasParam("full"))
        {
            String fullParam = request->getParam("full")->value();
            fullStatus = (fullParam == "true" || fullParam == "1");
        }

        if (fullStatus)
        {
            ESP_LOGD(TAG, "Getting full stepper status (with TMC2209)");
        }
        else
        {
            ESP_LOGD(TAG, "Getting basic stepper status (no TMC2209)");
        }

        JsonDocument doc;
        doc["status"] = "ok";
        
        // Read basic status (fast operations)
        doc["enabled"] = stepperController->isEnabled();
        doc["isRunning"] = stepperController->isRunning();
        doc["microsteps"] = stepperController->getMicrosteps();
        doc["gearRatio"] = stepperController->getGearRatio();
        doc["speedHz"] = stepperController->getTargetSpeedHz();
        doc["stepperPosition"] = stepperController->getStepperPosition();
        doc["behaviorInProgress"] = stepperController->isBehaviorInProgress();

        // TMC2209 Settings (only if full=true)
        if (fullStatus)
        {
            // UART reads are slow and blocking - yield before starting to let async_tcp run
            vTaskDelay(pdMS_TO_TICKS(10)); // Yield before starting TMC reads
            JsonObject tmc = doc["tmc2209"].to<JsonObject>();
            tmc["rmsCurrent"] = stepperController->getTmcRmsCurrent();
            vTaskDelay(pdMS_TO_TICKS(25)); // Yield to other tasks (UART read is slow)
            tmc["csActual"] = stepperController->getTmcCsActual();
            vTaskDelay(pdMS_TO_TICKS(25));
            tmc["actualCurrent"] = stepperController->getTmcActualCurrent();
            vTaskDelay(pdMS_TO_TICKS(25));
            tmc["irun"] = stepperController->getTmcIrun();
            vTaskDelay(pdMS_TO_TICKS(25));
            tmc["ihold"] = stepperController->getTmcIhold();
            vTaskDelay(pdMS_TO_TICKS(25));
            tmc["enabled"] = stepperController->getTmcEnabled();
            vTaskDelay(pdMS_TO_TICKS(25));
            tmc["spreadCycle"] = stepperController->getTmcSpreadCycle();
            vTaskDelay(pdMS_TO_TICKS(25));
            tmc["pwmAutoscale"] = stepperController->getTmcPwmAutoscale();
            vTaskDelay(pdMS_TO_TICKS(25));
            tmc["blankTime"] = stepperController->getTmcBlankTime();
            vTaskDelay(pdMS_TO_TICKS(25)); // Final yield before response
        }
        
        // MQTT configuration and connection status (only if full=true)
        if (fullStatus && configManager != nullptr)
        {
            const SystemConfig& cfg = configManager->getConfig();
            JsonObject mqtt = doc["mqtt"].to<JsonObject>();
            mqtt["enabled"] = cfg.mqttEnabled;
            mqtt["connected"] = (mqttController != nullptr && mqttController->isConnected());
            mqtt["broker"] = cfg.mqttBroker;
            mqtt["port"] = cfg.mqttPort;
            mqtt["deviceId"] = cfg.mqttDeviceId;
            mqtt["baseTopic"] = cfg.mqttBaseTopic;
        }

        String responseStr;
        serializeJson(doc, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperTurntablePosition(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/turntablePosition");

    if (request->method() == HTTP_GET)
    {
        ESP_LOGD(TAG, "Getting turntable position");

        JsonDocument doc;
        doc["status"] = "ok";

        doc["turntablePositionDegrees"] = stepperController->getStepperPositionDegrees();

        String responseStr;
        serializeJson(doc, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperPositionStatus(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/positionStatus");

    if (request->method() == HTTP_GET)
    {
        ESP_LOGD(TAG, "Getting position status");

        JsonDocument doc;
        doc["status"] = "ok";
        doc["stepperPosition"] = stepperController->getStepperPosition();
        doc["positionDegrees"] = stepperController->getStepperPositionDegrees();

        String responseStr;
        serializeJson(doc, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperHeading(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/heading");

    if (request->method() == HTTP_GET)
    {
        // Return current heading
        ESP_LOGD(TAG, "Getting current heading");

        // Get current heading from stepper position
        float currentHeading = stepperController->getStepperPositionDegrees();

        // Normalize to 0-360 range
        while (currentHeading < 0.0f)
        {
            currentHeading += 360.0f;
        }
        while (currentHeading >= 360.0f)
        {
            currentHeading -= 360.0f;
        }

        JsonDocument response;
        response["status"] = "ok";
        response["currentHeading"] = currentHeading;

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else if (request->method() == HTTP_POST)
    {
        // Extract and validate JSON body
        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            ESP_LOGW(TAG, "Body validation error: %s", errorMsg.c_str());
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        // Parse JSON into struct
        HeadingRequest req;
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        // Deserialize into struct
        if (doc["heading"].is<float>() || doc["heading"].is<int>())
        {
            req.heading = doc["heading"].as<float>();
        }
        else
        {
            sendErrorResponse(request, 400, "Missing or invalid 'heading' parameter (float: 0-360 degrees)");
            return;
        }

        ESP_LOGI(TAG, "Parsed values: heading=%.2f", req.heading);

        // Move to heading using shortest path
        bool success = stepperController->moveToHeadingDegrees(req.heading);

        if (!success)
        {
            sendErrorResponse(request, 500, "Failed to move to heading - stepper not available");
            return;
        }

        // Get current position for response
        float currentHeading = stepperController->getStepperPositionDegrees();

        JsonDocument response;
        response["status"] = "ok";
        response["message"] = "Moving to heading " + String(req.heading, 2) + "° (shortest path)";
        response["targetHeading"] = req.heading;
        response["currentHeading"] = currentHeading;

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperBehavior(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/behavior");

    if (request->method() == HTTP_GET)
    {
        ESP_LOGI(TAG, "Getting available behaviors");

        JsonDocument doc;
        doc["status"] = "ok";

        JsonArray behaviors = doc["behaviors"].to<JsonArray>();

        auto addBehavior = [&](const char* id, const char* name, const char* description) {
            JsonObject obj = behaviors.add<JsonObject>();
            obj["id"] = id;
            obj["name"] = name;
            obj["description"] = description;
        };

        addBehavior("scanning", "Scanning", "Slow 360° sweeps scanning for enemies");
        addBehavior("sleeping", "Sleeping", "Minimal subtle movements while resting");
        addBehavior("eating", "Eating", "Rhythmic chewing-like motions");
        addBehavior("alert", "Alert", "Rapid, jerky scanning when threatened");
        addBehavior("roaring", "Roaring", "Large intimidating sweeps and spins");
        addBehavior("stalking", "Stalking", "Slow deliberate movements with freezes");
        addBehavior("playing", "Playing", "Erratic playful movements");
        addBehavior("resting", "Resting", "Idle with occasional minor adjustments");
        addBehavior("hunting", "Hunting", "Focused sector scanning");
        addBehavior("victory", "Victory", "Celebratory spins and sweeps");

        String responseStr;
        serializeJson(doc, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else if (request->method() == HTTP_POST)
    {
        ESP_LOGI(TAG, "Starting behavior");

        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            ESP_LOGW(TAG, "Body validation error: %s", errorMsg.c_str());
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        if (error)
        {
            ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        if (!doc["behaviorType"].is<const char *>() && !doc["behaviorType"].is<String>())
        {
            sendErrorResponse(request, 400, "Missing or invalid 'behaviorType' parameter");
            return;
        }

        String behaviorStr = doc["behaviorType"].as<String>();
        behaviorStr.toLowerCase();

        StepperMotorController::BehaviorType behaviorType;
        if (behaviorStr == "scanning")
            behaviorType = StepperMotorController::BehaviorType::SCANNING;
        else if (behaviorStr == "sleeping")
            behaviorType = StepperMotorController::BehaviorType::SLEEPING;
        else if (behaviorStr == "eating")
            behaviorType = StepperMotorController::BehaviorType::EATING;
        else if (behaviorStr == "alert")
            behaviorType = StepperMotorController::BehaviorType::ALERT;
        else if (behaviorStr == "roaring")
            behaviorType = StepperMotorController::BehaviorType::ROARING;
        else if (behaviorStr == "stalking")
            behaviorType = StepperMotorController::BehaviorType::STALKING;
        else if (behaviorStr == "playing")
            behaviorType = StepperMotorController::BehaviorType::PLAYING;
        else if (behaviorStr == "resting")
            behaviorType = StepperMotorController::BehaviorType::RESTING;
        else if (behaviorStr == "hunting")
            behaviorType = StepperMotorController::BehaviorType::HUNTING;
        else if (behaviorStr == "victory")
            behaviorType = StepperMotorController::BehaviorType::VICTORY;
        else
        {
            sendErrorResponse(request, 400, "Unknown behaviorType");
            return;
        }

        bool started = stepperController->startBehavior(behaviorType);
        if (!started)
        {
            sendErrorResponse(request, 503, "Behavior already running or stepper unavailable");
            return;
        }

        JsonDocument response;
        response["status"] = "ok";
        response["message"] = "Behavior started";
        response["behaviorType"] = behaviorStr;

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperStopBehavior(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/stopBehavior");

    if (request->method() == HTTP_POST)
    {
        ESP_LOGI(TAG, "Stopping behavior");

        bool success = stepperController->stopBehavior();

        JsonDocument response;
        response["status"] = success ? "ok" : "error";
        response["message"] = success ? "Behavior stopped" : "No behavior in progress";

        String responseStr;
        serializeJson(response, responseStr);
        request->send(success ? 200 : 400, "application/json", responseStr);
        if (success) {
            ESP_LOGI(TAG, "Response: 200 OK");
        } else {
            ESP_LOGW(TAG, "Response: 400 Bad Request");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}


void HTTPServerController::handleStepperDance(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/dance");

    if (request->method() == HTTP_GET)
    {
        // Return list of available dance types
        ESP_LOGI(TAG, "Getting available dance types");

        JsonDocument doc;
        doc["status"] = "ok";
        
        JsonArray dances = doc["dances"].to<JsonArray>();
        
        JsonObject twist = dances.add<JsonObject>();
        twist["id"] = "twist";
        twist["name"] = "Twist";
        twist["description"] = "Chubby Checkers 'Twist' pattern with increasing/decreasing arcs";
        
        JsonObject shake = dances.add<JsonObject>();
        shake["id"] = "shake";
        shake["name"] = "Shake";
        shake["description"] = "Quick shake with rapid back and forth movements";
        
        JsonObject spin = dances.add<JsonObject>();
        spin["id"] = "spin";
        spin["name"] = "Spin";
        spin["description"] = "Full rotations back and forth";
        
        JsonObject wiggle = dances.add<JsonObject>();
        wiggle["id"] = "wiggle";
        wiggle["name"] = "Wiggle";
        wiggle["description"] = "Small wiggles in place";
        
        JsonObject watusi = dances.add<JsonObject>();
        watusi["id"] = "watusi";
        watusi["name"] = "Watusi";
        watusi["description"] = "Side-to-side alternating movements with increasing amplitude";
        
        JsonObject peppermintTwist = dances.add<JsonObject>();
        peppermintTwist["id"] = "peppermintTwist";
        peppermintTwist["name"] = "Peppermint Twist";
        peppermintTwist["description"] = "Rapid alternating twists back and forth";

        String responseStr;
        serializeJson(doc, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else if (request->method() == HTTP_POST)
    {
        // Extract and validate JSON body
        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            ESP_LOGW(TAG, "Body validation error: %s", errorMsg.c_str());
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        // Parse JSON into struct
        DanceRequest req;
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        // Deserialize into struct
        if (doc["danceType"].is<const char *>() || doc["danceType"].is<String>())
        {
            req.danceType = doc["danceType"].as<String>();
        }
        else
        {
            sendErrorResponse(request, 400, "Missing or invalid 'danceType' parameter (string: 'twist', 'shake', 'spin', 'wiggle', 'watusi', or 'peppermintTwist')");
            return;
        }

        ESP_LOGI(TAG, "Parsed values: danceType=%s", req.danceType.c_str());

        // Convert string to DanceType enum
        StepperMotorController::DanceType danceType;
        String danceTypeLower = req.danceType;
        danceTypeLower.toLowerCase();
        
        if (danceTypeLower == "twist")
        {
            danceType = StepperMotorController::DanceType::TWIST;
        }
        else if (danceTypeLower == "shake")
        {
            danceType = StepperMotorController::DanceType::SHAKE;
        }
        else if (danceTypeLower == "spin")
        {
            danceType = StepperMotorController::DanceType::SPIN;
        }
        else if (danceTypeLower == "wiggle")
        {
            danceType = StepperMotorController::DanceType::WIGGLE;
        }
        else if (danceTypeLower == "watusi")
        {
            danceType = StepperMotorController::DanceType::WATUSI;
        }
        else if (danceTypeLower == "pepperminttwist" || danceTypeLower == "peppermint_twist")
        {
            danceType = StepperMotorController::DanceType::PEPPERMINT_TWIST;
        }
        else
        {
            sendErrorResponse(request, 400, "Invalid danceType. Must be 'twist', 'shake', 'spin', 'wiggle', 'watusi', or 'peppermintTwist'");
            return;
        }

        // Start dance in background (non-blocking)
        // Return immediately to avoid blocking the HTTP handler task
        bool success = stepperController->startDance(danceType);

        JsonDocument response;
        response["status"] = success ? "ok" : "error";
        response["message"] = success ? "Dance started: " + req.danceType : "Dance failed to start";
        response["danceType"] = req.danceType;

        String responseStr;
        serializeJson(response, responseStr);
        request->send(success ? 200 : 500, "application/json", responseStr);
        if (success) {
            ESP_LOGI(TAG, "Response: 200 OK - Dance started");
        } else {
            ESP_LOGE(TAG, "Response: 500 Internal Server Error");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperStopDance(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/stopDance");

    if (request->method() == HTTP_POST)
    {
        ESP_LOGI(TAG, "Stopping dance");

        // Stop the dance
        bool success = stepperController->stopDance();

        JsonDocument response;
        response["status"] = success ? "ok" : "error";
        response["message"] = success ? "Dance stopped" : "No dance in progress";

        String responseStr;
        serializeJson(response, responseStr);
        request->send(success ? 200 : 400, "application/json", responseStr);
        if (success) {
            ESP_LOGI(TAG, "Response: 200 OK");
        } else {
            ESP_LOGW(TAG, "Response: 400 Bad Request");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleStepperRunning(AsyncWebServerRequest *request)
{
    logRequest(request, "/stepper/running");

    if (request->method() == HTTP_GET)
    {
        ESP_LOGD(TAG, "Getting stepper running status");

        JsonDocument doc;
        doc["status"] = "ok";
        doc["isRunning"] = stepperController->isRunning();

        String responseStr;
        serializeJson(doc, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK");
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleMqttConfig(AsyncWebServerRequest *request)
{
    logRequest(request, "/mqtt/config");

    if (request->method() == HTTP_GET)
    {
        // Return current MQTT configuration
        if (configManager == nullptr)
        {
            sendErrorResponse(request, 500, "Configuration manager not available");
            return;
        }

        const SystemConfig& config = configManager->getConfig();

        JsonDocument response;
        response["status"] = "ok";
        response["enabled"] = config.mqttEnabled;
        response["broker"] = config.mqttBroker;
        response["port"] = config.mqttPort;
        response["username"] = config.mqttUsername;
        response["password"] = config.mqttPassword;
        response["deviceId"] = config.mqttDeviceId;
        response["baseTopic"] = config.mqttBaseTopic;
        response["qosCommands"] = config.mqttQosCommands;
        response["qosStatus"] = config.mqttQosStatus;
        response["keepalive"] = config.mqttKeepalive;

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK - MQTT config returned");
    }
    else if (request->method() == HTTP_POST)
    {
        // Update MQTT configuration
        if (configManager == nullptr)
        {
            sendErrorResponse(request, 500, "Configuration manager not available");
            return;
        }

        // Extract and validate JSON body
        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            ESP_LOGW(TAG, "Body validation error: %s", errorMsg.c_str());
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        // Parse JSON
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
            ESP_LOGW(TAG, "JSON parse error: %s", error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        // Update configuration (only update fields that are provided)
        bool configChanged = false;
        
        if (doc["enabled"].is<bool>())
        {
            configManager->setMqttEnabled(doc["enabled"].as<bool>());
            configChanged = true;
        }
        if (doc["broker"].is<const char*>())
        {
            configManager->setMqttBroker(doc["broker"].as<const char*>());
            configChanged = true;
        }
        if (doc["port"].is<uint16_t>())
        {
            configManager->setMqttPort(doc["port"].as<uint16_t>());
            configChanged = true;
        }
        if (doc["username"].is<const char*>())
        {
            configManager->setMqttUsername(doc["username"].as<const char*>());
            configChanged = true;
        }
        if (doc["password"].is<const char*>())
        {
            configManager->setMqttPassword(doc["password"].as<const char*>());
            configChanged = true;
        }
        if (doc["deviceId"].is<const char*>())
        {
            configManager->setMqttDeviceId(doc["deviceId"].as<const char*>());
            configChanged = true;
        }
        if (doc["baseTopic"].is<const char*>())
        {
            configManager->setMqttBaseTopic(doc["baseTopic"].as<const char*>());
            configChanged = true;
        }
        if (doc["qosCommands"].is<uint8_t>())
        {
            configManager->setMqttQosCommands(doc["qosCommands"].as<uint8_t>());
            configChanged = true;
        }
        if (doc["qosStatus"].is<uint8_t>())
        {
            configManager->setMqttQosStatus(doc["qosStatus"].as<uint8_t>());
            configChanged = true;
        }
        if (doc["keepalive"].is<uint16_t>())
        {
            configManager->setMqttKeepalive(doc["keepalive"].as<uint16_t>());
            configChanged = true;
        }

        // Save configuration if changed
        if (configChanged)
        {
            if (configManager->save())
            {
                // Restart MQTT if controller is available and MQTT is enabled
                if (mqttController != nullptr)
                {
                    const SystemConfig& savedConfig = configManager->getConfig();
                    if (savedConfig.mqttEnabled)
                    {
                        ESP_LOGI(TAG, "Restarting MQTT with new configuration...");
                        mqttController->restart();
                    }
                    else
                    {
                        // If MQTT was disabled, disconnect if currently connected
                        if (mqttController->isConnected())
                        {
                            ESP_LOGI(TAG, "MQTT disabled - disconnecting...");
                            // MQTTController doesn't have a public disconnect, but restart will handle it
                        }
                    }
                }
                
                JsonDocument response;
                response["status"] = "ok";
                response["message"] = "MQTT configuration saved successfully";
                
                String responseStr;
                serializeJson(response, responseStr);
                request->send(200, "application/json", responseStr);
                ESP_LOGI(TAG, "Response: 200 OK - MQTT config saved");
            }
            else
            {
                sendErrorResponse(request, 500, "Failed to save configuration");
            }
        }
        else
        {
            JsonDocument response;
            response["status"] = "ok";
            response["message"] = "No changes detected";
            
            String responseStr;
            serializeJson(response, responseStr);
            request->send(200, "application/json", responseStr);
            ESP_LOGI(TAG, "Response: 200 OK - No changes");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Response: 405 Method Not Allowed");
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleMotorConfig(AsyncWebServerRequest *request)
{
    logRequest(request, "/motor/config");

    if (request->method() == HTTP_GET)
    {
        if (configManager == nullptr)
        {
            sendErrorResponse(request, 500, "Configuration manager not available");
            return;
        }

        const SystemConfig &config = configManager->getConfig();

        JsonDocument response;
        response["status"] = "ok";
        response["motorMaxSpeed"] = config.motorMaxSpeed;
        response["motorAcceleration"] = config.motorAcceleration;
        response["motorMicrosteps"] = config.motorMicrosteps;
        response["motorGearRatio"] = config.motorGearRatio;
        response["tmcRmsCurrent"] = config.tmcRmsCurrent;
        response["tmcIrun"] = config.tmcIrun;
        response["tmcIhold"] = config.tmcIhold;
        response["tmcSpreadCycle"] = config.tmcSpreadCycle;
        response["tmcPwmAutoscale"] = config.tmcPwmAutoscale;
        response["tmcBlankTime"] = config.tmcBlankTime;

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK - Motor config returned");
    }
    else if (request->method() == HTTP_POST)
    {
        if (configManager == nullptr)
        {
            sendErrorResponse(request, 500, "Configuration manager not available");
            return;
        }

        String body;
        String errorMsg;
        if (!getJsonBody(request, body, errorMsg))
        {
            sendErrorResponse(request, 400, errorMsg);
            return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        if (error)
        {
            sendErrorResponse(request, 400, "Invalid JSON: " + String(error.c_str()));
            return;
        }

        bool configChanged = false;

        if (doc["motorMaxSpeed"].is<float>())
        {
            float v = doc["motorMaxSpeed"].as<float>();
            if (v > 0 && v <= 10000)
            {
                configManager->setMotorMaxSpeed(v);
                configChanged = true;
            }
        }
        if (doc["motorAcceleration"].is<float>())
        {
            float v = doc["motorAcceleration"].as<float>();
            if (v > 0 && v <= 10000)
            {
                configManager->setMotorAcceleration(v);
                configChanged = true;
            }
        }
        if (doc["motorMicrosteps"].is<int>() || doc["motorMicrosteps"].is<float>())
        {
            int v = doc["motorMicrosteps"].as<int>();
            if (v >= 1 && v <= 256)
            {
                configManager->setMotorMicrosteps((uint8_t)v);
                configChanged = true;
            }
        }
        if (doc["motorGearRatio"].is<float>())
        {
            float v = doc["motorGearRatio"].as<float>();
            if (v > 0 && v <= 100)
            {
                configManager->setMotorGearRatio(v);
                configChanged = true;
            }
        }
        if (doc["tmcRmsCurrent"].is<int>() || doc["tmcRmsCurrent"].is<float>())
        {
            int v = doc["tmcRmsCurrent"].as<int>();
            if (v >= 0 && v <= 2000)
            {
                configManager->setTmcRmsCurrent((uint16_t)v);
                configChanged = true;
            }
        }
        if (doc["tmcIrun"].is<int>() || doc["tmcIrun"].is<float>())
        {
            int v = doc["tmcIrun"].as<int>();
            if (v >= 0 && v <= 31)
            {
                configManager->setTmcIrun((uint8_t)v);
                configChanged = true;
            }
        }
        if (doc["tmcIhold"].is<int>() || doc["tmcIhold"].is<float>())
        {
            int v = doc["tmcIhold"].as<int>();
            if (v >= 0 && v <= 31)
            {
                configManager->setTmcIhold((uint8_t)v);
                configChanged = true;
            }
        }
        if (doc["tmcSpreadCycle"].is<bool>())
        {
            configManager->setTmcSpreadCycle(doc["tmcSpreadCycle"].as<bool>());
            configChanged = true;
        }
        if (doc["tmcPwmAutoscale"].is<bool>())
        {
            configManager->setTmcPwmAutoscale(doc["tmcPwmAutoscale"].as<bool>());
            configChanged = true;
        }
        if (doc["tmcBlankTime"].is<int>() || doc["tmcBlankTime"].is<float>())
        {
            int v = doc["tmcBlankTime"].as<int>();
            if (v >= 0 && v <= 24)
            {
                configManager->setTmcBlankTime((uint8_t)v);
                configChanged = true;
            }
        }

        if (configChanged && configManager->save())
        {
            const SystemConfig &saved = configManager->getConfig();
            stepperController->setMaxSpeed(saved.motorMaxSpeed);
            stepperController->setAcceleration(saved.motorAcceleration);
            stepperController->setMicrosteps(saved.motorMicrosteps);
            stepperController->setGearRatio(saved.motorGearRatio);
            stepperController->setTmcRmsCurrent(saved.tmcRmsCurrent);
            stepperController->setTmcIrun(saved.tmcIrun);
            stepperController->setTmcIhold(saved.tmcIhold);
            stepperController->setTmcSpreadCycle(saved.tmcSpreadCycle);
            stepperController->setTmcPwmAutoscale(saved.tmcPwmAutoscale);
            stepperController->setTmcBlankTime(saved.tmcBlankTime);
        }

        JsonDocument response;
        response["status"] = "ok";
        response["message"] = configChanged ? "Motor and TMC configuration saved and applied" : "No changes detected";

        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
        ESP_LOGI(TAG, "Response: 200 OK - Motor config");
    }
    else
    {
        sendErrorResponse(request, 405, "Method not allowed");
    }
}

void HTTPServerController::handleNotFound(AsyncWebServerRequest *request)
{
    ESP_LOGW(TAG, "404 Not Found - %s %s from %s",
             request->method() == HTTP_GET ? "GET" : request->method() == HTTP_POST ? "POST" : "UNKNOWN",
             request->url().c_str(),
             request->client()->remoteIP().toString().c_str());
    sendErrorResponse(request, 404, "Not found");
}

// Reusable body handler for JSON POST requests
ArBodyHandlerFunction jsonBodyHandler = [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    // Body handler - collect body chunks and store in request's _tempObject
    if (index == 0)
    {
        request->_tempObject = new String();
    }
    String *body = (String *)request->_tempObject;
    for (size_t i = 0; i < len; i++)
    {
        *body += (char)data[i];
    }
    // Body is stored in _tempObject and will be accessed directly in getJsonBody()
    // Note: We don't delete the body here - it will be cleaned up when the request is destroyed
};

void HTTPServerController::setupRoutes()
{
    // Root and status routes
    server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleRoot(request); });
    server->on("/status", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleStatus(request); });

    // Stepper motor routes - with body handler for POST requests that need JSON
    server->on("/stepper/position", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleMoveTo(request); }, nullptr, jsonBodyHandler);
    server->on("/stepper/position", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleMoveTo(request); });
    server->on("/stepper/enable", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperEnable(request); }, nullptr, jsonBodyHandler);
    server->on("/stepper/speed", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperSpeed(request); }, nullptr, jsonBodyHandler);
    server->on("/stepper/acceleration", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperAcceleration(request); }, nullptr, jsonBodyHandler);
    server->on("/stepper/microsteps", HTTP_ANY, [this](AsyncWebServerRequest *request)
               { this->handleStepperMicrosteps(request); }, nullptr, jsonBodyHandler);
    server->on("/stepper/gearratio", HTTP_ANY, [this](AsyncWebServerRequest *request)
               { this->handleStepperGearRatio(request); }, nullptr, jsonBodyHandler);
    server->on("/stepper/speedHz", HTTP_ANY, [this](AsyncWebServerRequest *request)
               { this->handleStepperSpeedHz(request); }, nullptr, jsonBodyHandler);
    server->on("/stepper/runForward", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperRunForward(request); });
    server->on("/stepper/runBackward", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperRunBackward(request); });
    server->on("/stepper/stopMove", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperStopMove(request); });
    server->on("/stepper/forceStop", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperForceStop(request); });
    server->on("/stepper/reset", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperReset(request); });
    server->on("/stepper/status", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleStepperStatus(request); });
    server->on("/stepper/positionStatus", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleStepperPositionStatus(request); });
    server->on("/stepper/turntablePosition", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleStepperTurntablePosition(request); });
    server->on("/stepper/heading", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleStepperHeading(request); });
    server->on("/stepper/heading", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperHeading(request); }, nullptr, jsonBodyHandler);
    server->on("/stepper/dance", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleStepperDance(request); });
    server->on("/stepper/dance", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperDance(request); }, nullptr, jsonBodyHandler);
    server->on("/stepper/behavior", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleStepperBehavior(request); });
    server->on("/stepper/behavior", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperBehavior(request); }, nullptr, jsonBodyHandler);
    server->on("/stepper/stopBehavior", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperStopBehavior(request); });
    server->on("/stepper/stopDance", HTTP_POST, [this](AsyncWebServerRequest *request)
               { this->handleStepperStopDance(request); });
    server->on("/stepper/running", HTTP_GET, [this](AsyncWebServerRequest *request)
               { this->handleStepperRunning(request); });

    // Motor & TMC configuration
    server->on("/motor/config", HTTP_ANY, [this](AsyncWebServerRequest *request)
               { this->handleMotorConfig(request); }, nullptr, jsonBodyHandler);

    // MQTT configuration routes
    server->on("/mqtt/config", HTTP_ANY, [this](AsyncWebServerRequest *request)
               { this->handleMqttConfig(request); }, nullptr, jsonBodyHandler);

    server->onNotFound([this](AsyncWebServerRequest *request)
                       { this->handleNotFound(request); });
}

bool HTTPServerController::begin(StepperMotorController *stepperCtrl, MotorCommandQueue *cmdQueue, ConfigurationManager *configMgr, MQTTController *mqttCtrl)
{
    stepperController = stepperCtrl;
    commandQueue = cmdQueue;
    configManager = configMgr;
    mqttController = mqttCtrl;

    if (!initWiFi())
    {
        return false;
    }

    // Create AsyncWebServer instance
    server = new AsyncWebServer(httpPort);

    // Setup routes
    setupRoutes();

    // Start server (AsyncWebServer starts automatically)
    server->begin();
    ESP_LOGI(TAG, "HTTP server started");

    return true;
}

bool HTTPServerController::isConnected() const
{
    return WiFi.status() == WL_CONNECTED;
}

String HTTPServerController::getIPAddress() const
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return WiFi.localIP().toString();
    }
    return String("");
}

void HTTPServerController::printEndpoints() const
{
    if (!isConnected())
    {
        return;
    }

    String ip = getIPAddress();
    ESP_LOGI(TAG, "HTTP API Endpoints:");
    ESP_LOGI(TAG, "  http://%s/ - Web interface", ip.c_str());
    ESP_LOGI(TAG, "  GET http://%s/status", ip.c_str());
    ESP_LOGI(TAG, "Stepper Motor Endpoints:");
    ESP_LOGI(TAG, "  POST http://%s/stepper/position?position=90.0", ip.c_str());
    ESP_LOGI(TAG, "  GET http://%s/stepper/position", ip.c_str());
    ESP_LOGI(TAG, "  GET http://%s/stepper/position", ip.c_str());
    ESP_LOGI(TAG, "  POST http://%s/stepper/zero", ip.c_str());
    ESP_LOGI(TAG, "  POST http://%s/stepper/enable?enable=1", ip.c_str());
    ESP_LOGI(TAG, "  POST http://%s/stepper/speed?speed=1000", ip.c_str());
    ESP_LOGI(TAG, "  GET http://%s/stepper/status", ip.c_str());
    ESP_LOGI(TAG, "Configuration:");
    ESP_LOGI(TAG, "  GET/POST http://%s/motor/config - Motor & TMC settings (JSON)", ip.c_str());
    ESP_LOGI(TAG, "  GET/POST http://%s/mqtt/config - MQTT settings (JSON)", ip.c_str());
}
