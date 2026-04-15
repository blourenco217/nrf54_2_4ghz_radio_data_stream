#!/usr/bin/env python3
"""UART Client for receiving synchronized data from nRF54 Central device.

This client reads data from the serial port connected to the nRF54 Central device
and displays the synchronized timestamp and dummy data being transmitted from
the peripheral through the central.
"""

import serial
import serial.tools.list_ports
import sys
import re
import argparse
from datetime import datetime
from typing import Optional


class UARTDataReader:
    """Reader for UART data from nRF54 central device."""

    # Packet size from firmware (8 bytes timestamp + 4 bytes counter + 236 bytes dummy)
    PACKET_SIZE_BYTES = 248

    def __init__(self, port: str, baudrate: int = 1_000_000):
        """Initialize UART reader.
        
        Args:
            port: Serial port name (e.g., 'COM3' on Windows, '/dev/ttyUSB0' on Linux)
            baudrate: Baud rate for serial communication (default: 1000000)
        """
        self.port = port
        self.baudrate = baudrate
        self.ser: Optional[serial.Serial] = None
        # Support both old format and new format with optional peer field
        self.data_pattern = re.compile(r'DATA,(?:peer=(\d+),)?timestamp=(\d+),(?:value|counter)=([-\d]+)(?:,size=(\d+))?')

    def connect(self) -> bool:
        """Connect to the serial port.
        
        Returns:
            True if connection successful, False otherwise
        """
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=1,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                bytesize=serial.EIGHTBITS
            )
            print(f"Connected to {self.port} at {self.baudrate} baud")
            return True
        except serial.SerialException as e:
            print(f"Error connecting to {self.port}: {e}")
            return False

    def disconnect(self):
        """Disconnect from the serial port."""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("Disconnected from serial port")

    def parse_data_line(self, line: str) -> Optional[dict]:
        """Parse a data line from UART output.
        
        Args:
            line: Line of text from UART
            
        Returns:
            Dictionary with parsed data or None if not a data line
        """
        match = self.data_pattern.search(line)
        if match:
            result = {
                'peer_id': int(match.group(1)) if match.group(1) is not None else 0,
                'timestamp_us': int(match.group(2)),
                'counter': int(match.group(3)),
                'received_at': datetime.now()
            }
            if match.group(4):  # size field is optional
                result['size'] = int(match.group(4))
            return result
        return None

    def read_loop(self, verbose: bool = False):
        """Main read loop for UART data.
        
        Args:
            verbose: If True, print all UART output; if False, only print parsed data
        """
        if not self.ser or not self.ser.is_open:
            print("Not connected to serial port")
            return

        from collections import defaultdict

        print("Reading data... (Press Ctrl+C to stop)\n")
        header = f"{'Local Time':<20} {'Peer':<6} {'Timestamp (us)':<20} {'Counter':<10}"
        if verbose:
            header += f" {'Size (bytes)':<15}"
        print(header)
        print("-" * (56 if not verbose else 71))

        # Per-peer statistics tracking
        peer_stats = defaultdict(lambda: {'values': [], 'timestamps': [], 'packet_count': 0})

        try:
            while True:
                if self.ser.in_waiting > 0:
                    try:
                        line = self.ser.readline().decode('utf-8', errors='ignore').strip()

                        if verbose and line:
                            print(f"[RAW] {line}")

                        data = self.parse_data_line(line)
                        if data:
                            peer_id = data['peer_id']
                            time_str = data['received_at'].strftime('%H:%M:%S.%f')[:-3]
                            output = f"{time_str:<20} {peer_id:<6} {data['timestamp_us']:<20} {data['counter']:<10}"
                            if verbose and 'size' in data:
                                output += f" {data['size']:<15}"
                            print(output)

                            # Track per-peer statistics
                            ps = peer_stats[peer_id]
                            ps['values'].append(data['counter'])
                            ps['timestamps'].append(data['received_at'])
                            ps['packet_count'] += 1

                    except UnicodeDecodeError:
                        pass  # Skip malformed data

        except KeyboardInterrupt:
            print("\n\nStopped by user")
            for peer_id in sorted(peer_stats.keys()):
                ps = peer_stats[peer_id]
                print(f"\n{'='*60}")
                print(f"PEER {peer_id} STATISTICS")
                self._print_statistics(ps['packet_count'], ps['values'], ps['timestamps'])
        finally:
            self.disconnect()

    def _print_statistics(self, packet_count: int, values: list, timestamps: list):
        """Print statistics about received data.

        Args:
            packet_count: Total number of packets received
            values: List of received values
            timestamps: List of timestamps when packets were received
        """
        print("=" * 60)
        print("RECEPTION STATISTICS")
        print("=" * 60)
        
        if packet_count == 0:
            print("No data packets received")
            return
        
        print(f"Total packets received: {packet_count}")
        
        # Calculate packet loss from counter sequence
        if len(values) > 1:
            expected_count = values[-1] - values[0] + 1
            
            print(f"\nPacket Loss Analysis:")
            print(f"  First counter:      {values[0]}")
            print(f"  Last counter:       {values[-1]}")
            
            gaps = []
            for i in range(1, len(values)):
                increment = values[i] - values[i-1]
                if increment != 1:
                    gap_size = increment - 1
                    gaps.append((values[i-1], values[i], gap_size))

            lost_packets = max(0, expected_count - len(values))
            loss_rate = (lost_packets / expected_count * 100) if expected_count > 0 else 0

            print(f"  Expected packets:   {expected_count}")
            print(f"  Received packets:   {len(values)}")
            print(f"  Lost packets:       {lost_packets}")
            print(f"  Loss rate:          {loss_rate:.2f}%")

            if gaps:
                print(f"\n  Detected {len(gaps)} gap(s):")
                for prev_val, next_val, gap_size in gaps[:5]:  # Show first 5 gaps
                    print(f"    Gap after counter {prev_val}: missing {gap_size} packet(s) (next: {next_val})")
                if len(gaps) > 5:
                    print(f"    ... and {len(gaps) - 5} more gap(s)")
        
        # Calculate average frequency
        if len(timestamps) > 1:
            time_diffs = []
            for i in range(1, len(timestamps)):
                diff = (timestamps[i] - timestamps[i-1]).total_seconds()
                time_diffs.append(diff)
            
            avg_interval = sum(time_diffs) / len(time_diffs)
            avg_frequency = 1.0 / avg_interval if avg_interval > 0 else 0
            min_interval = min(time_diffs)
            max_interval = max(time_diffs)
            
            print(f"\nTiming Analysis:")
            print(f"  Average interval:   {avg_interval*1000:.2f} ms")
            print(f"  Average frequency:  {avg_frequency:.2f} Hz")
            print(f"  Min interval:       {min_interval*1000:.2f} ms")
            print(f"  Max interval:       {max_interval*1000:.2f} ms")
            
            # Calculate total duration
            total_duration = (timestamps[-1] - timestamps[0]).total_seconds()
            print(f"  Total duration:     {total_duration:.2f} seconds")
            
            # Calculate bit rate
            total_bytes = packet_count * self.PACKET_SIZE_BYTES
            total_bits = total_bytes * 8
            uart_bit_rate = total_bits / total_duration if total_duration > 0 else 0
            
            print(f"\nData Throughput:")
            print(f"  Packet size:        {self.PACKET_SIZE_BYTES} bytes")
            print(f"  Total bytes:        {total_bytes:,} bytes ({total_bytes/1024:.2f} KB)")

            print(f"  Bit rate:           {uart_bit_rate:,.2f} bps ({uart_bit_rate/1000:.2f} kbps)")
            if uart_bit_rate >= 1_000_000:
                print(f"  Bit rate:           {uart_bit_rate/1_000_000:.2f} Mbps")
            print(f"  Byte rate:          {uart_bit_rate/8:,.2f} bytes/s ({uart_bit_rate/8/1024:.2f} KB/s)")
            print(f"  Packet rate:        {packet_count/total_duration:.2f} packets/s")
        
        print("=" * 60)


def list_serial_ports():
    """List available serial ports."""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found")
        return
    
    print("Available serial ports:")
    for port in ports:
        print(f"  {port.device}: {port.description}")


def main():
    """Main entry point for UART client."""
    parser = argparse.ArgumentParser(
        description='UART Client for nRF54 synchronized data reception'
    )
    parser.add_argument(
        '-p', '--port',
        help='Serial port (e.g., COM3, /dev/ttyUSB0)'
    )
    parser.add_argument(
        '-b', '--baudrate',
        type=int,
        default=1_000_000,
        help='Baud rate (default: 1000000)'
    )
    parser.add_argument(
        '-l', '--list',
        action='store_true',
        help='List available serial ports'
    )
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='Print all UART output (not just parsed data)'
    )
    
    args = parser.parse_args()
    
    if args.list:
        list_serial_ports()
        return
    
    if not args.port:
        print("Error: Port not specified\n")
        list_serial_ports()
        print("\nUsage: python client_uart.py -p <port>")
        sys.exit(1)
    
    reader = UARTDataReader(args.port, args.baudrate)
    
    if reader.connect():
        reader.read_loop(verbose=args.verbose)


if __name__ == '__main__':
    main()
