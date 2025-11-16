==== README ====
# Overview
This repository contains a network transport client implementation. During testing, 
Valgrind reports memory errors due to uninitialized stack-allocated buffers in the 
test clients. This document outlines the affected components, the root cause, and 
suggested fixes.

# Component Status Summary
### 1. l2sap.c
- **Tested With:** Packet loss values from '-p 0.0' to '-p 0.04'  
- **Errors Observed:** 4 Valgrind errors  
- **Notes:** These errors are expected and stem from the behavior of 'datalink-test-client'.

### 2. l4sap.c
- **Tested With:**  
  - '-p 0.0' to '-p 0.03': 16 Valgrind errors  
  - '-p 0.04': 17 Valgrind errors

# Root Cause
The issue arises from an uninitialized stack-allocated buffer used in both 
'datalink-test-client' and 'transport-test-client'. This buffer is passed to 
'send()'-related functions and copied into outgoing packets. Since the buffer 
is not fully initialized, Valgrind detects the usage of uninitialized bytes 
during the 'sendto()' system call.

An additional error at '-p 0.04' is likely caused by increased retries due to higher packet
loss, leading to a second 'sendto()' call using the same partially uninitialized buffer.

# Problem Summary
Valgrind detects uninitialized memory usage in the 'l4sap_send()' function ('l4sap.c'), 
traced back to stack buffers in test clients that are not zeroed before use.

# Suggested Fix
Ensure all buffers are fully initialized before use. For example:
memset(buffer, 0, sizeof(buffer));
