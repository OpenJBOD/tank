/*
 * Copyright (c) 2024, Witekio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

async function fetchNetworkSettings()
{
    try {
        const response = await fetch("/api/settings");
        if (!response.ok) {
            throw new Error(`Response status: ${response.status}`);
        }

        const json = await response.json();
        
        // Populate form fields with current settings from the network section
        if (json.network) {
            document.getElementById("hostname").value = json.network.hostname || "openjbod";
            
            // Set IP method radio buttons
            if (json.network.ip_method === "static") {
                document.getElementById("ip_static").checked = true;
            } else {
                document.getElementById("ip_dhcp").checked = true;
            }
            
            // Populate IP settings
            document.getElementById("ip_address").value = json.network.ip_addr || "";
            document.getElementById("subnet_mask").value = json.network.ip_mask || "";
            document.getElementById("gateway_ip").value = json.network.gw_addr || "";
            document.getElementById("dns_ip").value = json.network.dns1 || "";
            
            // Populate IPv6 settings
            const ipv6ModeSelect = document.getElementById("ipv6_mode");
            if (ipv6ModeSelect) {
                ipv6ModeSelect.value = json.network.ipv6_mode || "slaac";
            }
            document.getElementById("ipv6_address").value = json.network.ipv6_addr || "";
            document.getElementById("ipv6_prefix_length").value = typeof json.network.ipv6_prefix_length === "number"
                ? json.network.ipv6_prefix_length
                : 64;
            document.getElementById("ipv6_gateway").value = json.network.ipv6_gateway || "";
            document.getElementById("ipv6_dns").value = json.network.ipv6_dns1 || "";
            
            // Update field states after setting the radio buttons
            if (window.updateFieldStates) {
                window.updateFieldStates();
            }
            if (window.updateIpv6FieldStates) {
                window.updateIpv6FieldStates();
            }
        } else {
            // Set default values if no network settings found
            document.getElementById("hostname").value = "openjbod";
            document.getElementById("ip_dhcp").checked = true;
            document.getElementById("ipv6_mode").value = "slaac";
            
            // Update field states for default case
            if (window.updateFieldStates) {
                window.updateFieldStates();
            }
            if (window.updateIpv6FieldStates) {
                window.updateIpv6FieldStates();
            }
        }
        
    } catch (error) {
        console.error("Failed to fetch network settings:", error.message);
        // Set default values on error
        document.getElementById("hostname").value = "openjbod";
        document.getElementById("ip_dhcp").checked = true;
        document.getElementById("ipv6_mode").value = "slaac";
        
        // Update field states for error case
        if (window.updateFieldStates) {
            window.updateFieldStates();
        }
        if (window.updateIpv6FieldStates) {
            window.updateIpv6FieldStates();
        }
    }
}

async function saveNetworkSettings(formData)
{
    try {
        // Format the data to match the expected settings structure
        const settingsData = {
            network: {
                hostname: formData.hostname,
                ip_method: formData.method, // Send as string: "dhcp" or "static"
                ip_addr: formData.ip_address || "",
                ip_mask: formData.subnet_mask || "",
                gw_addr: formData.gateway || "",
                dns1: formData.dns || "",
                ipv6_mode: formData.ipv6_mode || "slaac",
                ipv6_addr: formData.ipv6_address || "",
                ipv6_prefix_length: formData.ipv6_prefix_length ?? 64,
                ipv6_gateway: formData.ipv6_gateway || "",
                ipv6_dns1: formData.ipv6_dns || ""
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
        
        alert("Network settings saved successfully! Changes will take effect after reboot.");
        
    } catch (error) {
        console.error("Failed to save network settings:", error.message);
        alert("Failed to save network settings: " + error.message);
    }
}

window.addEventListener("DOMContentLoaded", (ev) => {
    // Load current network settings on page load
    fetchNetworkSettings();
    
    // Handle form submission
    const form = document.getElementById("network-form");
    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        
        const formData = new FormData(form);
        const settings = {
            hostname: formData.get("hostname"),
            method: formData.get("ip_method"),
            ip_address: formData.get("ip_address"),
            subnet_mask: formData.get("subnet_mask"),
            gateway: formData.get("gateway_ip"),
            dns: formData.get("dns_ip"),
            ipv6_mode: formData.get("ipv6_mode"),
            ipv6_address: formData.get("ipv6_address"),
            ipv6_prefix_length: (() => {
                const raw = formData.get("ipv6_prefix_length");
                const parsed = parseInt(raw, 10);
                return Number.isNaN(parsed) ? 64 : parsed;
            })(),
            ipv6_gateway: formData.get("ipv6_gateway"),
            ipv6_dns: formData.get("ipv6_dns")
        };
        
        console.log("Saving network settings:", settings);
        await saveNetworkSettings(settings);
    });
    
    // Enable/disable static IP fields based on method selection
    const dhcpRadio = document.getElementById("ip_dhcp");
    const staticRadio = document.getElementById("ip_static");
    const staticFields = ["ip_address", "subnet_mask", "gateway_ip", "dns_ip"];
    const ipv6ModeSelect = document.getElementById("ipv6_mode");
    const ipv6StaticFields = ["ipv6_address", "ipv6_prefix_length", "ipv6_gateway", "ipv6_dns"];
    
    window.updateFieldStates = function() {
        const isStatic = staticRadio.checked;
        staticFields.forEach(fieldId => {
            const field = document.getElementById(fieldId);
            field.disabled = !isStatic;
            if (!isStatic) {
                field.style.opacity = "0.5";
            } else {
                field.style.opacity = "1";
            }
        });
    }

    window.updateIpv6FieldStates = function() {
        const mode = ipv6ModeSelect.value;
        const isStatic = mode === "static";
        ipv6StaticFields.forEach(fieldId => {
            const field = document.getElementById(fieldId);
            field.disabled = !isStatic;
            field.style.opacity = isStatic ? "1" : "0.5";
        });
    }
    
    dhcpRadio.addEventListener("change", updateFieldStates);
    staticRadio.addEventListener("change", updateFieldStates);
    ipv6ModeSelect.addEventListener("change", updateIpv6FieldStates);
    
    // Initial state update
    updateFieldStates();
    updateIpv6FieldStates();
});