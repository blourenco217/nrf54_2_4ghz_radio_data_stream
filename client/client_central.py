#!/usr/bin/env python3
"""Direct BLE client for receiving data notifications from conn_time_sync peripheral.

This client bypasses the nRF54 central board and connects directly to a BLE peripheral
that exposes the conn_time_sync service. It subscribes to data notifications, prints
received packets in real time, and reports statistics when stopped.
"""

import argparse
import asyncio
import struct
import sys
from dataclasses import dataclass
from datetime import datetime
from typing import Optional

from bleak import BleakClient
from bleak import BleakScanner


# UUIDs from include/conn_time_sync.h
CONN_TIME_SYNC_SERVICE_UUID = "a88278d0-7009-4bee-a6f8-e1dc3ff02b92"
DATA_NOTIFY_CHAR_UUID = "a88278d2-7009-4bee-a6f8-e1dc3ff02b92"
CONFIG_BT_DEVICE_NAME="BLE Data Transmission"

@dataclass
class ReceivedPacket:
	"""Decoded peripheral notification packet."""

	timestamp_us: int
	counter: int
	size: int
	received_at: datetime


class BLEDataReader:
	"""BLE data receiver with packet loss and throughput statistics."""

	def __init__(
		self,
		address: Optional[str],
		name: Optional[str],
		service_uuid: str,
		char_uuid: str,
		scan_timeout_s: float,
	):
		self.address = address
		self.name = name
		self.service_uuid = service_uuid
		self.char_uuid = char_uuid
		self.scan_timeout_s = scan_timeout_s

		self.client: Optional[BleakClient] = None
		self.packet_count = 0
		self.total_bytes = 0
		self.counters: list[int] = []
		self.host_timestamps: list[datetime] = []
		self.stop_event = asyncio.Event()

	async def discover_target(self):
		"""Find target device by address or name. Skips service UUID check during scan."""

		print("Scanning for BLE device...")

		def device_filter(device, adv_data):
			# Exact address match if provided
			if self.address:
				return device.address.lower() == self.address.lower()
			
			# Name substring match if provided
			if self.name:
				device_name = (device.name or "").lower()
				return self.name.lower() in device_name
			
			# Accept any device if no filters
			return True

		device = await BleakScanner.find_device_by_filter(
			device_filter,
			timeout=self.scan_timeout_s,
		)

		if device:
			print(f"Found device: {device.name or 'Unknown'} ({device.address})")

		return device

	def _parse_notification(self, data: bytearray) -> Optional[ReceivedPacket]:
		"""Parse peripheral_data struct from notification bytes.

		Expected layout (little-endian):
		  uint64_t timestamp_us
		  int32_t  counter
		  uint8_t  dummy_data[]
		"""

		if len(data) < 12:
			return None

		timestamp_us, counter = struct.unpack_from("<Qi", data, 0)
		return ReceivedPacket(
			timestamp_us=timestamp_us,
			counter=counter,
			size=len(data),
			received_at=datetime.now(),
		)

	def _notification_callback(self, _: int, data: bytearray):
		packet = self._parse_notification(data)
		if not packet:
			print(f"[WARN] Short packet received ({len(data)} bytes)")
			return

		self.packet_count += 1
		self.total_bytes += packet.size
		self.counters.append(packet.counter)
		self.host_timestamps.append(packet.received_at)

		time_str = packet.received_at.strftime("%H:%M:%S.%f")[:-3]
		print(
			f"{time_str:<20} {packet.timestamp_us:<20} {packet.counter:<10} {packet.size:<12}"
		)

	async def run(self):
		"""Connect to BLE peripheral and receive notifications until interrupted."""
		device = await self.discover_target()
		if not device:
			print("No matching BLE peripheral found")
			return

		def on_disconnect(_: BleakClient):
			print("\nDisconnected from BLE peripheral")
			self.stop_event.set()

		self.client = BleakClient(device, disconnected_callback=on_disconnect)

		await self.client.connect()
		print("Connected")

		print("\nReading notifications... (Press Ctrl+C to stop)\n")
		print(f"{'Local Time':<20} {'Timestamp (us)':<20} {'Counter':<10} {'Size (bytes)':<12}")
		print("-" * 66)

		await self.client.start_notify(self.char_uuid, self._notification_callback)

		try:
			await self.stop_event.wait()
		finally:
			if self.client.is_connected:
				await self.client.stop_notify(self.char_uuid)
				await self.client.disconnect()

	def print_statistics(self):
		"""Print packet loss, timing, and throughput statistics."""
		print("\n" + "=" * 60)
		print("RECEPTION STATISTICS")
		print("=" * 60)

		if self.packet_count == 0:
			print("No data packets received")
			print("=" * 60)
			return

		print(f"Total packets received: {self.packet_count}")

		if len(self.counters) > 1:
			first_counter = self.counters[0]
			last_counter = self.counters[-1]
			expected_count = max(0, last_counter - first_counter + 1)

			gaps = []
			non_monotonic = 0
			for i in range(1, len(self.counters)):
				delta = self.counters[i] - self.counters[i - 1]
				if delta > 1:
					gaps.append((self.counters[i - 1], self.counters[i], delta - 1))
				elif delta <= 0:
					non_monotonic += 1

			lost_packets = max(0, expected_count - len(self.counters))
			loss_rate = (lost_packets / expected_count * 100) if expected_count > 0 else 0.0

			print("\nPacket Loss Analysis:")
			print(f"  First counter:      {first_counter}")
			print(f"  Last counter:       {last_counter}")
			print(f"  Expected packets:   {expected_count}")
			print(f"  Received packets:   {len(self.counters)}")
			print(f"  Lost packets:       {lost_packets}")
			print(f"  Loss rate:          {loss_rate:.2f}%")

			if non_monotonic > 0:
				print(f"  Non-monotonic steps: {non_monotonic} (counter reset/reorder may have happened)")

			if gaps:
				print(f"\n  Detected {len(gaps)} gap(s):")
				for prev_val, next_val, gap_size in gaps[:5]:
					print(
						f"    Gap after counter {prev_val}: missing {gap_size} packet(s) (next: {next_val})"
					)
				if len(gaps) > 5:
					print(f"    ... and {len(gaps) - 5} more gap(s)")

		if len(self.host_timestamps) > 1:
			time_diffs_s = [
				(self.host_timestamps[i] - self.host_timestamps[i - 1]).total_seconds()
				for i in range(1, len(self.host_timestamps))
			]

			avg_interval = sum(time_diffs_s) / len(time_diffs_s)
			avg_frequency = (1.0 / avg_interval) if avg_interval > 0 else 0.0
			min_interval = min(time_diffs_s)
			max_interval = max(time_diffs_s)
			total_duration = (
				self.host_timestamps[-1] - self.host_timestamps[0]
			).total_seconds()

			print("\nTiming Analysis:")
			print(f"  Average interval:   {avg_interval * 1000:.2f} ms")
			print(f"  Average frequency:  {avg_frequency:.2f} Hz")
			print(f"  Min interval:       {min_interval * 1000:.2f} ms")
			print(f"  Max interval:       {max_interval * 1000:.2f} ms")
			print(f"  Total duration:     {total_duration:.2f} seconds")

			bit_rate = (self.total_bytes * 8 / total_duration) if total_duration > 0 else 0.0
			byte_rate = bit_rate / 8
			packet_rate = (self.packet_count / total_duration) if total_duration > 0 else 0.0

			print("\nData Throughput:")
			print(f"  Total bytes:        {self.total_bytes:,} bytes ({self.total_bytes / 1024:.2f} KB)")
			print(f"  Bit rate:           {bit_rate:,.2f} bps ({bit_rate / 1000:.2f} kbps)")
			if bit_rate >= 1_000_000:
				print(f"  Bit rate:           {bit_rate / 1_000_000:.2f} Mbps")
			print(f"  Byte rate:          {byte_rate:,.2f} bytes/s ({byte_rate / 1024:.2f} KB/s)")
			print(f"  Packet rate:        {packet_rate:.2f} packets/s")

		print("=" * 60)


def main():
	parser = argparse.ArgumentParser(
		description="Direct BLE client for conn_time_sync peripheral data notifications"
	)
	parser.add_argument(
		"-a",
		"--address",
		help="Target peripheral BLE address (optional)",
	)
	parser.add_argument(
		"-n",
		"--name",
		help="Target peripheral name substring (optional)",
	)
	parser.add_argument(
		"--service-uuid",
		default=CONN_TIME_SYNC_SERVICE_UUID,
		help=f"Service UUID filter (default: {CONN_TIME_SYNC_SERVICE_UUID})",
	)
	parser.add_argument(
		"--char-uuid",
		default=DATA_NOTIFY_CHAR_UUID,
		help=f"Notify characteristic UUID (default: {DATA_NOTIFY_CHAR_UUID})",
	)
	parser.add_argument(
		"--scan-timeout",
		type=float,
		default=10.0,
		help="BLE scan timeout in seconds (default: 10)",
	)

	args = parser.parse_args()

	# Default to BLE Data Transmission if no name or address provided
	target_name = args.name if args.name else CONFIG_BT_DEVICE_NAME
	target_address = args.address

	print(f"Searching for: {target_address or target_name}")

	reader = BLEDataReader(
		address=target_address,
		name=target_name,
		service_uuid=args.service_uuid,
		char_uuid=args.char_uuid,
		scan_timeout_s=args.scan_timeout,
	)

	try:
		asyncio.run(reader.run())
	except KeyboardInterrupt:
		print("\n\nStopped by user")
	except Exception as exc:
		print(f"Fatal error: {exc}")
		sys.exit(1)
	finally:
		reader.print_statistics()


if __name__ == "__main__":
	main()
