/*
 * Copyright (c) 2024, Witekio
 *
 * SPDX-License-Identifier: Apache-2.0
 */
function formatUptime(uptimeMs)
{
	const uptimeSeconds = Math.floor(uptimeMs / 1000);
	const uptimeMinutes = Math.floor(uptimeSeconds / 60);
	const uptimeHours = Math.floor(uptimeMinutes / 60);
	const uptimeDays = Math.floor(uptimeHours / 24);

	if (uptimeDays > 0) {
		return `${uptimeDays}d ${uptimeHours % 24}h ${uptimeMinutes % 60}m`;
	} else if (uptimeHours > 0) {
		return `${uptimeHours}h ${uptimeMinutes % 60}m`;
	} else if (uptimeMinutes > 0) {
		return `${uptimeMinutes}m ${uptimeSeconds % 60}s`;
	}
	return `${uptimeSeconds}s`;
}

async function postBeacon(on)
{
	try {
		const payload = JSON.stringify({"beacon" : on});

		const response = await fetch("/api/led", {method : "POST", body : payload});
		if (!response.ok) {
			throw new Error(`Response status: ${response.status}`);
		}
	} catch (error) {
		console.error(error.message);
	}
}

async function powerOn()
{
	try {
		const response = await fetch("/api/power/on", {method : "POST"});
		if (!response.ok) {
			throw new Error(`Response status: ${response.status}`);
		}
			fetchStatus(); // Update status after action
	} catch (error) {
		console.error(error.message);
	}
}

async function powerOff()
{
	try {
		const response = await fetch("/api/power/off", {method : "POST"});
		if (!response.ok) {
			throw new Error(`Response status: ${response.status}`);
		}
			fetchStatus(); // Update status after action
	} catch (error) {
		console.error(error.message);
	}
}

async function fetchStatus()
{
	try {
		const response = await fetch("/api/status");
		if (!response.ok) {
			throw new Error(`Response status: ${response.status}`);
		}

		const json = await response.json();

		// Update power status
		const powerElement = document.getElementById("power_status");
		if (json.power && typeof json.power.state === "string") {
			const state = json.power.state.toLowerCase();
			powerElement.innerHTML = state.toUpperCase();
			powerElement.className = state === "on" ? "status-on" : "status-off";
		} else {
			powerElement.innerHTML = "Unknown";
			powerElement.className = "status-unknown";
		}

		// Power settings (merged from the former /api/settings overview fetch)
		if (json.power) {
			document.getElementById("on_boot_enabled_display").innerHTML = json.power.on_boot ? "Enabled" : "Disabled";
			document.getElementById("on_boot_delay_display").innerHTML = json.power.on_boot_delay + " seconds";
			document.getElementById("follow_usb_enabled_display").innerHTML = json.power.follow_usb ? "Enabled" : "Disabled";
			document.getElementById("follow_usb_delay_display").innerHTML = json.power.follow_usb_delay + " seconds";
		}

		// Reflect the locator beacon state
		const beaconToggle = document.getElementById("beacon_toggle");
		if (beaconToggle && typeof json.beacon === "boolean") {
			beaconToggle.checked = json.beacon;
		}
		
		// Update temperature
		if (json.temperature && json.temperature.valid) {
			if (json.temperature.ds18b20.valid) {
				document.getElementById("temperature").innerHTML = json.temperature.ds18b20.temperature.toFixed(1) + "°C";
			} else if (json.temperature.rp2040.valid) {
				document.getElementById("temperature").innerHTML = json.temperature.rp2040.temperature.toFixed(1) + "°C (RP2040)";
			} else {
				document.getElementById("temperature").innerHTML = "No sensors";
			}
		} else {
			document.getElementById("temperature").innerHTML = "Error";
		}
		
		// Update fan status
		if (json.fan && json.fan.valid) {
			document.getElementById("fan_rpm").innerHTML = json.fan.rpm || "Unknown";
			document.getElementById("fan_speed").innerHTML = (json.fan.speed_percent || 0) + "%";
		} else {
			document.getElementById("fan_rpm").innerHTML = "Error";
			document.getElementById("fan_speed").innerHTML = "Error";
		}
		
		// Update device info
		if (json.device) {
			document.getElementById("serial_number").innerHTML = json.device.serial || "Unknown";
			document.getElementById("software_version").innerHTML = json.device.version || "0.0.1";
			document.getElementById("board_rev").innerHTML = json.device.board_revision || "Unknown";
			document.getElementById("hostname").innerHTML = json.device.hostname || "openjbod";
		} else {
			document.getElementById("serial_number").innerHTML = "Error";
			document.getElementById("software_version").innerHTML = "Error";
			document.getElementById("board_rev").innerHTML = "Error";
			document.getElementById("hostname").innerHTML = "Error";
		}
		
		// Update network info
		if (json.network) {
			document.getElementById("mac_address").innerHTML = json.network.mac_address || "Unknown";
			document.getElementById("ip_address").innerHTML = json.network.ip_address || "Unknown";
			document.getElementById("subnet_mask").innerHTML = json.network.subnet_mask || "Unknown";
			document.getElementById("gateway").innerHTML = json.network.gateway || "Unknown";
			document.getElementById("ip_method").innerHTML = json.network.ip_method || "Unknown";
			document.getElementById("ipv6_mode_status").innerHTML = json.network.ipv6_mode || "Unknown";
			const ipv6Cell = document.getElementById("ipv6_addresses");
			if (Array.isArray(json.network.ipv6_addresses) && json.network.ipv6_addresses.length > 0) {
				ipv6Cell.innerHTML = json.network.ipv6_addresses.join("<br>");
			} else {
				ipv6Cell.innerHTML = "None";
			}
		} else {
			document.getElementById("mac_address").innerHTML = "Error";
			document.getElementById("ip_address").innerHTML = "Error";
			document.getElementById("subnet_mask").innerHTML = "Error";
			document.getElementById("gateway").innerHTML = "Error";
			document.getElementById("ip_method").innerHTML = "Error";
			document.getElementById("ipv6_mode_status").innerHTML = "Error";
			document.getElementById("ipv6_addresses").innerHTML = "Error";
		}

		// Update uptime (merged from the former /uptime endpoint)
		const uptimeEl = document.getElementById("uptime");
		if (uptimeEl) {
			uptimeEl.innerHTML = (typeof json.uptime === "number")
				? formatUptime(json.uptime) : "Error";
		}

	} catch (error) {
		console.error("Failed to fetch status:", error.message);
		// Set error state for all fields
		const powerElement = document.getElementById("power_status");
		powerElement.innerHTML = "Error";
		powerElement.className = "status-unknown";
		document.getElementById("on_boot_enabled_display").innerHTML = "Error";
		document.getElementById("on_boot_delay_display").innerHTML = "Error";
		document.getElementById("follow_usb_enabled_display").innerHTML = "Error";
		document.getElementById("follow_usb_delay_display").innerHTML = "Error";
		document.getElementById("temperature").innerHTML = "Error";
		document.getElementById("fan_rpm").innerHTML = "Error";
		document.getElementById("fan_speed").innerHTML = "Error";
		document.getElementById("serial_number").innerHTML = "Error";
		document.getElementById("software_version").innerHTML = "Error";
		document.getElementById("board_rev").innerHTML = "Error";
		document.getElementById("mac_address").innerHTML = "Error";
		document.getElementById("ip_address").innerHTML = "Error";
		document.getElementById("subnet_mask").innerHTML = "Error";
		document.getElementById("gateway").innerHTML = "Error";
		document.getElementById("ip_method").innerHTML = "Error";
		document.getElementById("ipv6_mode_status").innerHTML = "Error";
		document.getElementById("ipv6_addresses").innerHTML = "Error";
		document.getElementById("uptime").innerHTML = "Error";
	}
}

window.addEventListener("DOMContentLoaded", (ev) => {
	/* One consolidated fetch drives the whole overview (power state + settings, temp,
	 * fan, device, network, uptime); refreshed on the interval below. */
	fetchStatus();

	/* Fetch dynamic data on intervals */
	setInterval(fetchStatus, 5000);  // Update status every 5 seconds

	/* Locator beacon toggle */
	const beacon_toggle = document.getElementById("beacon_toggle");
	beacon_toggle.addEventListener("change", (event) => {
		postBeacon(event.target.checked);
	})

	/* Handle power control buttons */
	const power_on_btn = document.getElementById("power_on");
	power_on_btn.addEventListener("click", (event) => {
		console.log("power_on clicked");
		if (!window.confirm("Power on the system?")) {
			return;
		}
		powerOn();
	})

	const power_off_btn = document.getElementById("power_off");
	power_off_btn.addEventListener("click", (event) => {
		console.log("power_off clicked");
		if (!window.confirm("Power off the system?")) {
			return;
		}
		powerOff();
	})

})
