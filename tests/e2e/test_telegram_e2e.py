#!/usr/bin/env python3
"""
GhostClaw Telegram Bot E2E Tests

This script tests the Telegram bot integration by:
1. Verifying bot token validity
2. Testing message sending
3. Testing update polling

Requirements:
- TELEGRAM_BOT_TOKEN environment variable
- TELEGRAM_TEST_CHAT_ID environment variable (optional, for send tests)

Run: python3 tests/e2e/test_telegram_e2e.py
"""

import os
import sys
import json
import time
import subprocess
from urllib.request import Request, urlopen
from urllib.error import URLError, HTTPError

# Colors for output
GREEN = '\033[0;32m'
RED = '\033[0;31m'
YELLOW = '\033[1;33m'
NC = '\033[0m'

PASSED = 0
FAILED = 0


def log_pass(msg):
    global PASSED
    print(f"{GREEN}[PASS]{NC} {msg}")
    PASSED += 1


def log_fail(msg, reason):
    global FAILED
    print(f"{RED}[FAIL]{NC} {msg}: {reason}")
    FAILED += 1


def log_info(msg):
    print(f"{YELLOW}[INFO]{NC} {msg}")


def log_skip(msg):
    print(f"{YELLOW}[SKIP]{NC} {msg}")


def telegram_api(token, method, data=None):
    """Make a Telegram API request."""
    url = f"https://api.telegram.org/bot{token}/{method}"
    
    try:
        if data:
            req = Request(url, data=json.dumps(data).encode('utf-8'))
            req.add_header('Content-Type', 'application/json')
        else:
            req = Request(url)
        
        with urlopen(req, timeout=10) as response:
            return json.loads(response.read().decode('utf-8'))
    except HTTPError as e:
        return {'ok': False, 'error_code': e.code, 'description': str(e)}
    except URLError as e:
        return {'ok': False, 'error_code': 0, 'description': str(e)}
    except Exception as e:
        return {'ok': False, 'error_code': 0, 'description': str(e)}


def main():
    log_info("Starting Telegram E2E tests...")
    
    # Get token from environment
    token = os.environ.get('TELEGRAM_BOT_TOKEN')
    chat_id = os.environ.get('TELEGRAM_TEST_CHAT_ID')
    
    if not token:
        log_info("TELEGRAM_BOT_TOKEN not set, checking .env file...")
        env_file = os.path.join(os.path.dirname(__file__), '..', '..', '.env')
        if os.path.exists(env_file):
            with open(env_file) as f:
                for line in f:
                    if line.startswith('TELEGRAM_BOT_TOKEN='):
                        token = line.split('=', 1)[1].strip().strip('"\'')
                        break
    
    if not token:
        log_skip("No TELEGRAM_BOT_TOKEN found - skipping Telegram tests")
        print("\nTo run Telegram tests, set TELEGRAM_BOT_TOKEN environment variable")
        return 0
    
    log_info(f"Using token: {token[:10]}...{token[-5:]}")
    
    # ============================================
    # Test 1: Bot token validity (getMe)
    # ============================================
    log_info("Test 1: Bot token validity")
    result = telegram_api(token, 'getMe')
    
    if result.get('ok'):
        bot_info = result.get('result', {})
        log_pass(f"Bot token valid - @{bot_info.get('username', 'unknown')}")
    else:
        log_fail("Bot token validity", result.get('description', 'Unknown error'))
        # If token is invalid, skip remaining tests
        return 1
    
    # ============================================
    # Test 2: Get updates (polling)
    # ============================================
    log_info("Test 2: Get updates")
    result = telegram_api(token, 'getUpdates', {'limit': 1, 'timeout': 0})
    
    if result.get('ok'):
        updates = result.get('result', [])
        log_pass(f"Get updates works - {len(updates)} pending updates")
    else:
        log_fail("Get updates", result.get('description', 'Unknown error'))
    
    # ============================================
    # Test 3: Send message (if chat_id provided)
    # ============================================
    if chat_id:
        log_info("Test 3: Send message")
        test_message = f"GhostClaw E2E test - {time.strftime('%Y-%m-%d %H:%M:%S')}"
        result = telegram_api(token, 'sendMessage', {
            'chat_id': chat_id,
            'text': test_message
        })
        
        if result.get('ok'):
            log_pass("Send message works")
        else:
            log_fail("Send message", result.get('description', 'Unknown error'))
    else:
        log_skip("Send message test - TELEGRAM_TEST_CHAT_ID not set")
    
    # ============================================
    # Test 4: Webhook info
    # ============================================
    log_info("Test 4: Webhook info")
    result = telegram_api(token, 'getWebhookInfo')
    
    if result.get('ok'):
        webhook_info = result.get('result', {})
        webhook_url = webhook_info.get('url', '')
        if webhook_url:
            log_pass(f"Webhook configured: {webhook_url[:30]}...")
        else:
            log_pass("No webhook configured (polling mode)")
    else:
        log_fail("Webhook info", result.get('description', 'Unknown error'))
    
    # ============================================
    # Test 5: Bot commands
    # ============================================
    log_info("Test 5: Get bot commands")
    result = telegram_api(token, 'getMyCommands')
    
    if result.get('ok'):
        commands = result.get('result', [])
        log_pass(f"Bot commands retrieved - {len(commands)} commands")
    else:
        log_fail("Bot commands", result.get('description', 'Unknown error'))
    
    # ============================================
    # Summary
    # ============================================
    print()
    print("============================================")
    print("Telegram E2E Test Results")
    print("============================================")
    print(f"Passed: {GREEN}{PASSED}{NC}")
    print(f"Failed: {RED}{FAILED}{NC}")
    print("============================================")
    
    return 1 if FAILED > 0 else 0


if __name__ == '__main__':
    sys.exit(main())
