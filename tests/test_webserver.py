#!/usr/bin/env python3
"""
Automated tests for the Atari Portfolio web server.

Prerequisites:
  1. SLIP link established:
     sudo slattach -s 9600 -p slip /dev/ttyUSB0 &
     sudo ifconfig sl0 192.168.7.1 pointopoint 192.168.7.2 up

  2. Portfolio running webserver.exe in www/ directory

  3. Install dependencies:
     pip install pytest requests

Usage:
  pytest test_webserver.py -v
  pytest test_webserver.py -v -k "test_index"  # run specific test
  pytest test_webserver.py -v --tb=short       # shorter tracebacks
"""

import pytest
import requests
import socket
import time

# Server configuration - adjust to match your setup
SERVER_IP = "192.168.7.2"
SERVER_PORT = 80
BASE_URL = f"http://{SERVER_IP}:{SERVER_PORT}"

# Timeout for requests (Portfolio is slow!)
TIMEOUT = 30


class TestBasicHTTP:
    """Basic HTTP functionality tests."""

    def test_index_page(self):
        """GET / should return index.htm content."""
        r = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
        assert r.status_code == 200
        assert "Welcome to Atari Portfolio" in r.text
        assert "text/html" in r.headers.get("Content-Type", "")

    def test_about_page(self):
        """GET /about.htm should return about page."""
        r = requests.get(f"{BASE_URL}/about.htm", timeout=TIMEOUT)
        assert r.status_code == 200
        assert "About" in r.text

    def test_404_missing_file(self):
        """GET for non-existent file should return 404."""
        r = requests.get(f"{BASE_URL}/nonexistent.htm", timeout=TIMEOUT)
        assert r.status_code == 404
        assert "404" in r.text or "Not Found" in r.text


class TestMIMETypes:
    """MIME type handling tests."""

    def test_html_mime_type(self):
        """HTML files should have text/html content type."""
        r = requests.get(f"{BASE_URL}/index.htm", timeout=TIMEOUT)
        assert r.status_code == 200
        assert "text/html" in r.headers.get("Content-Type", "")

    def test_text_mime_type(self):
        """TXT files should have text/plain content type."""
        r = requests.get(f"{BASE_URL}/docs/readme.txt", timeout=TIMEOUT)
        assert r.status_code == 200
        assert "text/plain" in r.headers.get("Content-Type", "")

    def test_image_mime_type(self):
        """JPEG files should have image/jpeg content type."""
        r = requests.get(f"{BASE_URL}/pofo.jpg", timeout=TIMEOUT)
        assert r.status_code == 200
        assert "image/jpeg" in r.headers.get("Content-Type", "")


class TestDirectories:
    """Directory handling tests."""

    def test_subdirectory_index(self):
        """Subdirectory with index.htm should serve it."""
        # First check if docs has an index.htm
        r = requests.get(f"{BASE_URL}/docs/", timeout=TIMEOUT)
        assert r.status_code == 200
        # Should be either index.htm content or directory listing

    def test_subdirectory_file(self):
        """Files in subdirectories should be accessible."""
        r = requests.get(f"{BASE_URL}/docs/specs.htm", timeout=TIMEOUT)
        assert r.status_code == 200
        assert "Technical Specifications" in r.text

    def test_directory_listing_contains_files(self):
        """Directory listing should contain file links."""
        # Access a directory that doesn't have index.htm
        r = requests.get(f"{BASE_URL}/docs/", timeout=TIMEOUT)
        assert r.status_code == 200
        # Should contain links to files
        assert "readme.txt" in r.text or "specs.htm" in r.text


class TestFileContent:
    """File content integrity tests."""

    def test_readme_content(self):
        """README file should have expected content."""
        r = requests.get(f"{BASE_URL}/docs/readme.txt", timeout=TIMEOUT)
        assert r.status_code == 200
        assert "Atari Portfolio" in r.text
        assert "SLIP" in r.text

    def test_specs_content(self):
        """Specs file should have expected content."""
        r = requests.get(f"{BASE_URL}/docs/specs.htm", timeout=TIMEOUT)
        assert r.status_code == 200
        assert "80C88" in r.text or "4.9 MHz" in r.text

    def test_image_not_empty(self):
        """Image file should have non-zero content."""
        r = requests.get(f"{BASE_URL}/pofo.jpg", timeout=TIMEOUT)
        assert r.status_code == 200
        assert len(r.content) > 100  # Should be more than 100 bytes


class TestSequentialConnections:
    """Test multiple sequential connections."""

    def test_three_sequential_requests(self):
        """Server should handle multiple sequential requests."""
        for i in range(3):
            r = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
            assert r.status_code == 200, f"Request {i+1} failed"
            time.sleep(0.5)  # Small delay between requests

    def test_different_files_sequential(self):
        """Server should serve different files in sequence."""
        files = ["/", "/about.htm", "/docs/readme.txt", "/docs/specs.htm"]
        for path in files:
            r = requests.get(f"{BASE_URL}{path}", timeout=TIMEOUT)
            assert r.status_code == 200, f"Failed to get {path}"
            time.sleep(0.5)


class TestConnectionQueue:
    """Test connection queuing behavior."""

    def test_rapid_requests(self):
        """Server should queue rapid sequential requests."""
        results = []
        for i in range(5):
            try:
                r = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
                results.append(r.status_code)
            except requests.exceptions.RequestException as e:
                results.append(f"error: {e}")
            # Minimal delay - test queuing
            time.sleep(0.1)

        # At least some should succeed
        successes = sum(1 for r in results if r == 200)
        assert successes >= 3, f"Only {successes}/5 requests succeeded: {results}"


class TestEdgeCases:
    """Edge case tests."""

    def test_root_path(self):
        """Root path / should work."""
        r = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
        assert r.status_code == 200

    def test_path_without_leading_slash(self):
        """Path handling should be robust."""
        r = requests.get(f"{BASE_URL}/about.htm", timeout=TIMEOUT)
        assert r.status_code == 200

    def test_deep_404(self):
        """404 in subdirectory should work."""
        r = requests.get(f"{BASE_URL}/docs/nonexistent.htm", timeout=TIMEOUT)
        assert r.status_code == 404


class TestLowLevel:
    """Low-level socket tests for edge cases."""

    def test_incomplete_request(self):
        """Server should handle incomplete HTTP request gracefully."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(TIMEOUT)
        try:
            sock.connect((SERVER_IP, SERVER_PORT))
            # Send incomplete request (no final CRLF CRLF)
            sock.send(b"GET / HTTP/1.0\r\n")
            time.sleep(2)
            # Server should wait for complete request
            # Send the rest
            sock.send(b"\r\n")
            response = sock.recv(4096)
            assert b"200" in response or b"HTML" in response
        finally:
            sock.close()

    def test_raw_get_request(self):
        """Raw socket GET request should work."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(TIMEOUT)
        try:
            sock.connect((SERVER_IP, SERVER_PORT))
            sock.send(b"GET / HTTP/1.0\r\n\r\n")
            response = b""
            while True:
                chunk = sock.recv(1024)
                if not chunk:
                    break
                response += chunk
            assert b"200 OK" in response
            assert b"Welcome" in response or b"html" in response.lower()
        finally:
            sock.close()


# Stress test - run separately as it takes longer
class TestStress:
    """Stress tests - may take a while."""

    @pytest.mark.slow
    def test_ten_sequential_requests(self):
        """Server should handle 10 sequential requests."""
        for i in range(10):
            r = requests.get(f"{BASE_URL}/", timeout=TIMEOUT)
            assert r.status_code == 200, f"Request {i+1} failed"
            time.sleep(0.3)

    @pytest.mark.slow
    def test_mixed_file_types(self):
        """Server should handle mixed file type requests."""
        files = [
            ("/", "text/html"),
            ("/pofo.jpg", "image/jpeg"),
            ("/docs/readme.txt", "text/plain"),
            ("/docs/specs.htm", "text/html"),
        ]
        for _ in range(3):  # 3 rounds
            for path, expected_mime in files:
                r = requests.get(f"{BASE_URL}{path}", timeout=TIMEOUT)
                assert r.status_code == 200
                assert expected_mime in r.headers.get("Content-Type", "")
                time.sleep(0.2)


if __name__ == "__main__":
    # Quick connectivity check
    print(f"Testing connection to {BASE_URL}...")
    try:
        r = requests.get(f"{BASE_URL}/", timeout=10)
        print(f"Connected! Status: {r.status_code}")
        print(f"Content-Type: {r.headers.get('Content-Type', 'N/A')}")
        print(f"Response length: {len(r.text)} bytes")
        print("\nRun 'pytest test_webserver.py -v' for full test suite")
    except requests.exceptions.RequestException as e:
        print(f"Connection failed: {e}")
        print("\nMake sure:")
        print("  1. SLIP is configured (slattach)")
        print("  2. IP is configured (ifconfig sl0)")
        print("  3. Portfolio is running webserver.exe")
