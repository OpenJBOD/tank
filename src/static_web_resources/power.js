/*
 * Copyright (c) 2024, Witekio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

async function fetchPowerSettings()
{
    try {
        const response = await fetch("/api/settings");
        if (!response.ok) {
            throw new Error(`Response status: ${response.status}`);
        }

        const json = await response.json();
        
        // Populate form fields with current settings from the power section
        if (json.power) {
            document.getElementById("ignore_power_switch").checked = json.power.ignore_power_switch || false;
            document.getElementById("on_boot").checked = json.power.on_boot || false;
            document.getElementById("on_boot_delay").value = json.power.on_boot_delay || 0;
            document.getElementById("follow_usb").checked = json.power.follow_usb || false;
            document.getElementById("follow_usb_delay").value = json.power.follow_usb_delay || 5;
        } else {
            // Set default values if no power settings found
            document.getElementById("ignore_power_switch").checked = false;
            document.getElementById("on_boot").checked = false;
            document.getElementById("on_boot_delay").value = 0;
            document.getElementById("follow_usb").checked = false;
            document.getElementById("follow_usb_delay").value = 5;
        }
        
        // Update field states after loading settings
        updateFieldStates();
        
    } catch (error) {
        console.error("Failed to fetch power settings:", error.message);
        // Set default values on error
        document.getElementById("ignore_power_switch").checked = false;
        document.getElementById("on_boot").checked = false;
        document.getElementById("on_boot_delay").value = 0;
        document.getElementById("follow_usb").checked = false;
        document.getElementById("follow_usb_delay").value = 5;
        
        // Update field states for error case
        updateFieldStates();
    }
}

async function savePowerSettings(formData)
{
    try {
        // Validate mutual exclusivity
        if (formData.on_boot && formData.follow_usb) {
            throw new Error("On Boot and Follow USB Power cannot both be enabled at the same time. These settings are mutually exclusive.");
        }
        
        // Format the data to match the expected settings structure
        const settingsData = {
            power: {
                ignore_power_switch: formData.ignore_power_switch,
                on_boot: formData.on_boot,
                on_boot_delay: parseInt(formData.on_boot_delay) || 0,
                follow_usb: formData.follow_usb,
                follow_usb_delay: parseInt(formData.follow_usb_delay) || 5
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
            throw new Error(`Response status: ${response.status}`);
        }
        
        alert("Power settings saved successfully! Changes will take effect immediately.");
        
    } catch (error) {
        console.error("Failed to save power settings:", error.message);
        alert("Failed to save power settings: " + error.message);
    }
}

function updateFieldStates() {
    const followUsb = document.getElementById("follow_usb").checked;
    const onBoot = document.getElementById("on_boot").checked;
    const followUsbDelay = document.getElementById("follow_usb_delay");
    const onBootCheckbox = document.getElementById("on_boot");
    const followUsbCheckbox = document.getElementById("follow_usb");
    
    // Disable "On Boot" when "Follow USB" is enabled (mutually exclusive)
    onBootCheckbox.disabled = followUsb;
    if (followUsb) {
        onBootCheckbox.style.opacity = "0.5";
    } else {
        onBootCheckbox.style.opacity = "1";
    }
    
    // Disable "Follow USB" when "On Boot" is enabled (mutually exclusive)
    followUsbCheckbox.disabled = onBoot;
    if (onBoot) {
        followUsbCheckbox.style.opacity = "0.5";
    } else {
        followUsbCheckbox.style.opacity = "1";
    }
    
    // Enable/disable follow USB delay based on follow USB setting
    followUsbDelay.disabled = !followUsb;
    if (!followUsb) {
        followUsbDelay.style.opacity = "0.5";
    } else {
        followUsbDelay.style.opacity = "1";
    }
}

window.addEventListener("DOMContentLoaded", (ev) => {
    // Load current power settings on page load
    fetchPowerSettings();
    
    // Handle form submission
    const form = document.getElementById("power-form");
    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        
        const formData = new FormData(form);
        const settings = {
            ignore_power_switch: formData.get("ignore_power_switch") === "on",
            on_boot: formData.get("on_boot") === "on",
            on_boot_delay: formData.get("on_boot_delay"),
            follow_usb: formData.get("follow_usb") === "on",
            follow_usb_delay: formData.get("follow_usb_delay")
        };
        
        console.log("Saving power settings:", settings);
        await savePowerSettings(settings);
    });
    
    // Handle mutual exclusivity between Follow USB and On Boot
    const followUsbCheckbox = document.getElementById("follow_usb");
    const onBootCheckbox = document.getElementById("on_boot");
    followUsbCheckbox.addEventListener("change", updateFieldStates);
    onBootCheckbox.addEventListener("change", updateFieldStates);
    
    // Initial state update
    updateFieldStates();
});