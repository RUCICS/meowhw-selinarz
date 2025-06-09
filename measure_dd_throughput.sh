#!/bin/bash

echo "Buffer Size (KB),Throughput (GB/s)" > dd_throughput.csv

# Base buffer size from mycat4 (e.g., page size or st_blksize, assume 4KB for this example)
base_buf_size_bytes=4096
# Let's try multipliers that give us common buffer sizes like
# 4K, 8K, 16K, 32K, 64K, 128K, 256K, 512K, 1024K
# Multipliers: 1, 2, 4, 8, 16, 32, 64, 128, 256
multipliers=(1 2 4 8 16 32 64 128 256 512) # Multipliers for base_buf_size
total_data_to_transfer="2G" # Total data to transfer for each test

# Calculate count for dd based on total data and current bs
# For simplicity in parsing dd output, we'll let dd calculate count if bs is given and size is specified for if/of.
# Or, more robustly, calculate count explicitly.
# Size in bytes for 2G = 2 * 1024 * 1024 * 1024
total_data_bytes=$((2 * 1024 * 1024 * 1024))

echo "Running dd tests with various buffer sizes..."

# ... (脚本前面部分不变) ...

for multiplier in "${multipliers[@]}"; do
    current_bs_bytes=$((base_buf_size_bytes * multiplier))
    current_bs_kb=$((current_bs_bytes / 1024))
    
    count=$(( (total_data_bytes + current_bs_bytes - 1) / current_bs_bytes ))

    echo "Testing with buffer size: ${current_bs_kb}KB (Multiplier: ${multiplier}x, bs=${current_bs_bytes}, count=${count})"

    # Run dd and capture stderr (where speed is reported)
    # Force LANG=C to get consistent dd output format if possible
    output=$(LANG=C dd if=/dev/zero of=/dev/null bs=${current_bs_bytes} count=${count} 2>&1 | grep 'bytes.*copied')
    
    if [ -n "$output" ]; then
        # More robust parsing:
        # Example output: "2147483648 bytes (2.1 GB, 2.0 GiB) copied, 0.123 s, 17.4 GB/s"
        # Or: "2147483648 bytes (2.1 GB, 2.0 GiB) copied, 1.234 s, 1740 MB/s"
        
        # Extract the part after the last comma, which contains "speed unit"
        speed_part=$(echo "$output" | awk -F', ' '{print $NF}') # $NF should be "17.4 GB/s" or "1740 MB/s"
        
        speed_value=$(echo "$speed_part" | awk '{print $1}') # "17.4" or "1740"
        speed_unit=$(echo "$speed_part" | awk '{print $2}')  # "GB/s" or "MB/s"

        throughput_gb_s=""

        if [[ "$speed_unit" == "GB/s" ]]; then
            throughput_gb_s=$speed_value
        elif [[ "$speed_unit" == "MB/s" ]]; then
            # Convert MB/s to GB/s using bc for floating point arithmetic
            throughput_gb_s=$(echo "scale=3; $speed_value / 1024" | bc)
        elif [[ "$speed_unit" == "kB/s" || "$speed_unit" == "KB/s" ]]; then # Less likely for /dev/zero to /dev/null
            throughput_gb_s=$(echo "scale=6; $speed_value / 1024 / 1024" | bc)
        else
            echo "Warning: Unknown speed unit '$speed_unit' from dd output: $output"
            throughput_gb_s="ERROR" # Mark as error
        fi
        
        if [[ "$throughput_gb_s" != "ERROR" ]]; then
            echo "${current_bs_kb},${throughput_gb_s}" >> dd_throughput.csv
            echo "Throughput: ${throughput_gb_s} GB/s (Original: $speed_part)"
        else
            echo "${current_bs_kb},ERROR" >> dd_throughput.csv
            echo "Failed to parse throughput from dd output."
        fi
    else
        echo "Failed to get dd output for bs=${current_bs_kb}KB"
        echo "${current_bs_kb},ERROR" >> dd_throughput.csv
    fi
    sleep 1 # Small pause between tests
done

# ... (脚本后面 Python 部分不变) ...
echo "Experiment finished. Results in dd_throughput.csv"
echo "Now plotting the results..."

# Python script to plot the results
python3 - <<END_PYTHON_SCRIPT
import matplotlib.pyplot as plt
import pandas as pd

try:
    data = pd.read_csv('dd_throughput.csv')
    # Ensure throughput is numeric, coercing errors to NaN and dropping them
    data['Throughput (GB/s)'] = pd.to_numeric(data['Throughput (GB/s)'], errors='coerce')
    data.dropna(subset=['Throughput (GB/s)'], inplace=True)

    if not data.empty:
        plt.figure(figsize=(10, 6))
        plt.plot(data['Buffer Size (KB)'], data['Throughput (GB/s)'], marker='o')
        plt.title('dd Throughput vs. Buffer Size (Input: /dev/zero, Output: /dev/null)')
        plt.xlabel('Buffer Size (KB)')
        plt.ylabel('Throughput (GB/s)')
        plt.xscale('log', base=2) # Use log scale for buffer size for better visualization
        plt.xticks(data['Buffer Size (KB)'], data['Buffer Size (KB)'].astype(str)) # Show all x-ticks
        plt.grid(True, which="both", ls="--")
        plt.savefig('dd_throughput_plot.png')
        print("Plot saved as dd_throughput_plot.png")
        # plt.show() # Uncomment to display plot if running in a GUI environment
    else:
        print("No valid data to plot.")
except FileNotFoundError:
    print("Error: dd_throughput.csv not found. Run the bash script first.")
except Exception as e:
    print(f"An error occurred during plotting: {e}")

END_PYTHON_SCRIPT