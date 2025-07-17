import os
import sys

#!/usr/bin/env python3

HEADER_LINES_NUMBER=16
ignore_folders = ['build', 'autogen', 'config', 'mbedtls', 'lib', 'test']  # Add folder names to ignore

LICENSE_HEADER = [
    "* # License",
    "* <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>",
    "*******************************************************************************",
    "*",
    "* The licensor of this software is Silicon Laboratories Inc. Your use of this",
    "* software is governed by the terms of Silicon Labs Master Software License",
    "* Agreement (MSLA) available at",
    "* www.silabs.com/about-us/legal/master-software-license-agreement. This",
    "* software is distributed to you in Source Code format and is governed by the",
    "* sections of the MSLA applicable to Source Code.",
    "*",
    "******************************************************************************/",
]

def has_license_header(lines):
    header = [line.strip() for line in lines[4: 4+len(LICENSE_HEADER)]]
    return header == LICENSE_HEADER

def check_file(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            lines = [f.readline() for _ in range(HEADER_LINES_NUMBER)]
        return has_license_header(lines)
    except Exception as e:
        print(f"Error reading {filepath}: {e}")
        return False

def main():
    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
    missing_license = []

    for dirpath, dirnames, filenames in os.walk(root_dir):
        # Modify dirnames in-place to skip ignored folders
        dirnames[:] = [d for d in dirnames if d not in ignore_folders]
        for filename in filenames:
            if filename.endswith(('.c', '.h')):
                filepath = os.path.join(dirpath, filename)
                if not check_file(filepath):
                    missing_license.append(filepath)

    if missing_license:
        print("Files missing license header:")
        for f in missing_license:
            print(f)
        sys.exit(1)
    else:
        print("All .c and .h files have the license header.")

if __name__ == "__main__":
    main()
