#!/usr/bin/env python3
"""Generate a device-unique Zigbee 3.0 install code for *offline provisioning*.

Does NOT program ESP32-H2 or persist the code in files/repository.
Physical provisioning still needs Espressif SDK factory tool and explicit authorization.
"""
import secrets


def install_crc(code: bytes) -> int:
    if len(code) != 16:
        raise ValueError("Zigbee 3.0 install code payload must be exactly 16 bytes")
    crc = 0xFFFF
    for value in code:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0x8408 if (crc & 1) else 0)
    return crc ^ 0xFFFF


def generate() -> bytes:
    code = secrets.token_bytes(16)
    return code + install_crc(code).to_bytes(2, "little")


def main():
    value = generate()
    print("Zigbee3 install code (18 bytes incl CRC16, label in 2-byte groups):")
    print(" ".join(value.hex().upper()[i:i+4] for i in range(0,36,4)))
    print("Keep private, pair with physical IEEE EUI64 and provision using Espressif factory tooling.")
    print("This command DOES NOT install the secret onto the ESP32-H2.")


if __name__ == "__main__":
    main()
