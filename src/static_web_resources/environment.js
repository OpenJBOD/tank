/*
 * Copyright (c) 2024 OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

let currentEnvironmentSettings = null;

function getDefaultFanCurve()
{
    return [
        { temperature: 20.0, fan_percent: 0 },
        { temperature: 30.0, fan_percent: 20 },
        { temperature: 40.0, fan_percent: 40 },
        { temperature: 50.0, fan_percent: 70 },
        { temperature: 60.0, fan_percent: 100 }
    ];
}

async function fetchEnvironmentSettings()
{
    try {
        const response = await fetch("/api/settings");
        if (!response.ok) {
            throw new Error(`Response status: ${response.status}`);
        }

        const json = await response.json();
        
        // Populate form fields with current settings from the environment section
        if (json.environment) {
            const env = json.environment;
            currentEnvironmentSettings = JSON.parse(JSON.stringify(env));
            
            // Temperature source preference
            document.getElementById("primary_temp_source").value = String(env.primary_temp_source ?? 0);

            // Fan control settings
            document.getElementById("external_fan_control").checked = env.use_external_fan_control || false;
            document.getElementById("fan_update_interval").value = env.fan_update_interval_ms || 5000;
            document.getElementById("fan_hysteresis").value = env.fan_hysteresis_percent || 5;
            
            // Fan curve points
            if (env.fan_curve && Array.isArray(env.fan_curve)) {
                for (let i = 0; i < Math.min(5, env.fan_curve.length); i++) {
                    const point = env.fan_curve[i];
                    document.getElementById(`temp_${i}`).value = point.temperature || 0;
                    document.getElementById(`fan_${i}`).value = point.fan_percent || 0;
                }
            }
            
            // Update field states after loading settings
            updateFieldStates();
            
        } else {
            // Set default values if no environment settings found
            setDefaultValues();
        }
        
    } catch (error) {
        console.error("Failed to fetch environment settings:", error.message);
        // Set default values on error
        setDefaultValues();
    }
}

function setDefaultValues() {
    document.getElementById("external_fan_control").checked = false;
    document.getElementById("fan_update_interval").value = 5000;
    document.getElementById("fan_hysteresis").value = 5;

    const defaultCurve = getDefaultFanCurve();

    for (let i = 0; i < 5; i++) {
        document.getElementById(`temp_${i}`).value = defaultCurve[i].temperature;
        document.getElementById(`fan_${i}`).value = defaultCurve[i].fan_percent;
    }

    currentEnvironmentSettings = {
        use_external_fan_control: false,
        fan_update_interval_ms: 5000,
        fan_hysteresis_percent: 5,
        fan_curve: defaultCurve
    };

    updateFieldStates();
}

async function saveEnvironmentSettings(formData)
{
    try {
        const externalFanControl = formData.use_external_fan_control === true;
        let curve = [];

        if (externalFanControl) {
            const storedCurve = currentEnvironmentSettings?.fan_curve;
            if (storedCurve && Array.isArray(storedCurve)) {
                curve = storedCurve.map(point => ({
                    temperature: point.temperature,
                    fan_percent: point.fan_percent
                }));
            } else {
                curve = getDefaultFanCurve();
            }
        } else {
            for (let i = 0; i < 5; i++) {
                const temp = parseFloat(formData[`temp_${i}`]);
                const fan = parseInt(formData[`fan_${i}`], 10);

                if (isNaN(temp) || isNaN(fan)) {
                    throw new Error(`Invalid values at curve point ${i + 1}`);
                }

                if (temp < -40 || temp > 125) {
                    throw new Error(`Temperature at point ${i + 1} must be between -40°C and 125°C`);
                }

                if (fan < 0 || fan > 100) {
                    throw new Error(`Fan speed at point ${i + 1} must be between 0% and 100%`);
                }

                curve.push({
                    temperature: temp,
                    fan_percent: fan
                });
            }

            for (let i = 1; i < 5; i++) {
                if (curve[i].temperature <= curve[i - 1].temperature) {
                    throw new Error(`Temperature at point ${i + 1} must be higher than point ${i}`);
                }
            }
        }

        let updateInterval;
        if (externalFanControl) {
            updateInterval = currentEnvironmentSettings?.fan_update_interval_ms ?? 5000;
        } else {
            updateInterval = parseInt(formData.fan_update_interval_ms, 10);
            if (isNaN(updateInterval) || updateInterval < 1000 || updateInterval > 60000) {
                throw new Error("Update interval must be between 1000ms and 60000ms");
            }
        }

        let hysteresis;
        if (externalFanControl) {
            hysteresis = currentEnvironmentSettings?.fan_hysteresis_percent ?? 5;
        } else {
            hysteresis = parseInt(formData.fan_hysteresis_percent, 10);
            if (isNaN(hysteresis) || hysteresis < 0 || hysteresis > 50) {
                throw new Error("Hysteresis must be between 0% and 50%");
            }
        }

        const primarySource = parseInt(formData.primary_temp_source, 10) === 1 ? 1 : 0;

        const settingsData = {
            environment: {
                primary_temp_source: primarySource,
                use_external_fan_control: externalFanControl,
                fan_update_interval_ms: updateInterval,
                fan_hysteresis_percent: hysteresis,
                fan_curve: curve
            }
        };
        
        const response = await fetch("/api/settings", {
            method: "POST",
            headers: {
                "Content-Type": "application/json",
            },
            body: JSON.stringify(settingsData)
        });
        
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(`Response status: ${response.status} - ${errorText}`);
        }
        
        alert("Environment settings saved successfully!");
        currentEnvironmentSettings = settingsData.environment;
        
    } catch (error) {
        console.error("Failed to save environment settings:", error.message);
        alert("Failed to save environment settings: " + error.message);
    }
}



function updateFieldStates() {
    const externalFanControlEnabled = document.getElementById("external_fan_control").checked;
    
    // Enable/disable fan curve and settings based on fan control checkbox
    // When external fan control is ENABLED, disable internal curve settings
    // When external fan control is DISABLED, enable internal curve settings
    const fanFields = [
        "fan_update_interval", "fan_hysteresis",
        "temp_0", "fan_0", "temp_1", "fan_1", "temp_2", "fan_2", 
        "temp_3", "fan_3", "temp_4", "fan_4"
    ];
    
    fanFields.forEach(fieldId => {
        const field = document.getElementById(fieldId);
        field.disabled = externalFanControlEnabled;
        if (externalFanControlEnabled) {
            field.style.opacity = "0.5";
        } else {
            field.style.opacity = "1";
        }
    });
    
    // Update the curve container styling
    const curveContainer = document.querySelector(".fan-curve-container");
    if (curveContainer) {
        if (externalFanControlEnabled) {
            curveContainer.style.opacity = "0.5";
        } else {
            curveContainer.style.opacity = "1";
        }
    }
}

function validateTemperatureOrder() {
    // Validate that temperature values are in ascending order
    for (let i = 1; i < 5; i++) {
        const prevTemp = parseFloat(document.getElementById(`temp_${i-1}`).value);
        const currentTemp = parseFloat(document.getElementById(`temp_${i}`).value);
        
        if (!isNaN(prevTemp) && !isNaN(currentTemp) && currentTemp <= prevTemp) {
            document.getElementById(`temp_${i}`).setCustomValidity(
                `Temperature must be higher than point ${i} (${prevTemp}°C)`
            );
            return false;
        } else {
            document.getElementById(`temp_${i}`).setCustomValidity("");
        }
    }
    return true;
}

function formatProbe(sensor) {
    if (!sensor) {
        return "n/a";
    }
    if (sensor.present === false) {
        return "Not present";
    }
    if (!sensor.valid) {
        return "No reading";
    }
    return `${sensor.temperature.toFixed(1)} °C`;
}

async function fetchTemperatures() {
    try {
        const response = await fetch("/api/temp", { cache: "no-store" });
        if (!response.ok) {
            throw new Error(`Response status: ${response.status}`);
        }
        const t = await response.json();
        document.getElementById("temp-onboard").textContent = formatProbe(t.ds18b20);
        document.getElementById("temp-header").textContent = formatProbe(t.ds18b20_ext);
        document.getElementById("temp-rp2040").textContent = formatProbe(t.rp2040);

        const active = t.active_source || "unknown";
        const activeTemp = (typeof t.active_temperature === "number")
            ? ` (${t.active_temperature.toFixed(1)} °C)` : "";
        document.getElementById("temp-active").textContent = `${active}${activeTemp}`;
    } catch (error) {
        console.error("Failed to fetch temperatures:", error.message);
    }
}

window.addEventListener("DOMContentLoaded", (ev) => {
    // Load current environment settings on page load
    fetchEnvironmentSettings();

    // Live temperature readings
    fetchTemperatures();
    setInterval(fetchTemperatures, 3000);
    
    // Handle form submission
    const form = document.getElementById("environment-form");
    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        
        if (!validateTemperatureOrder()) {
            alert("Please ensure temperature values are in ascending order.");
            return;
        }
        
        const formData = new FormData(form);
        const settings = {
            primary_temp_source: formData.get("primary_temp_source"),
            use_external_fan_control: formData.get("use_external_fan_control") === "on",
            fan_update_interval_ms: formData.get("fan_update_interval_ms"),
            fan_hysteresis_percent: formData.get("fan_hysteresis_percent"),
        };
        
        // Add fan curve points
        for (let i = 0; i < 5; i++) {
            settings[`temp_${i}`] = formData.get(`temp_${i}`);
            settings[`fan_${i}`] = formData.get(`fan_${i}`);
        }
        
        console.log("Saving environment settings:", settings);
        await saveEnvironmentSettings(settings);
    });
    
    // Handle checkbox change to enable/disable fields
    const fanControlCheckbox = document.getElementById("external_fan_control");
    fanControlCheckbox.addEventListener("change", updateFieldStates);
    
    // Add event listeners for temperature validation
    for (let i = 0; i < 5; i++) {
        const tempField = document.getElementById(`temp_${i}`);
        tempField.addEventListener("input", validateTemperatureOrder);
    }
    
    // Initial state update
    updateFieldStates();
});
