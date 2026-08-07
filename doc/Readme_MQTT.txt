# TLS/SSL Certificates for Secure MQTT Connection

When using MQTT port 8883, the ESP32 will attempt to establish a TLS/SSL
encrypted connection to the broker. This requires a CA certificate.

## Required File

Place your CA certificate here as:
  ca.crt

## How to Obtain the CA Certificate

### Option 1: Self-signed or Private CA
If your MQTT broker uses a self-signed certificate or private CA:
- Export the CA certificate from your broker configuration
- Save it as ca.crt in PEM format

### Option 2: Public Broker with Let's Encrypt
For brokers using Let's Encrypt certificates:
- Download the ISRG Root X1 certificate
- Or use the chain certificate from your broker

### Option 3: Extract from Browser
1. Connect to your MQTT broker's web interface (if available)
2. Click the lock icon in the browser address bar
3. View certificate details
4. Export the root CA certificate in PEM format

## Certificate Format

The certificate must be in PEM format:
```
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
... (base64 encoded certificate data) ...
-----END CERTIFICATE-----
```

## Configuration

In config.yml, set port to 8883:
```yaml
mqtt:
  host: your-broker.example.com
  port: 8883
  username: your_username
  password: your_password
```

## Troubleshooting

If TLS connection fails:
1. Verify certificate is in correct PEM format
2. Check certificate is not expired
3. Ensure certificate matches the broker's CA chain
4. Check ESP32 serial output for TLS error messages

## File Size Limit

Maximum certificate size: 4096 bytes
Most CA certificates are well under this limit.

## Security Note

This folder is uploaded to the ESP32's SPIFFS filesystem.
Do NOT store private keys here - only CA (root) certificates.
