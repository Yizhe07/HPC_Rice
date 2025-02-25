#!/bin/bash
# run_experiment.sh
# This script runs othello-parallel with the default_input (lookahead depth 7)
# for thread counts 1 to 32, extracts timing information, and writes the results to a CSV file.

# Output file to store the results
output_file="execution_times.csv"

# Write the header to the output file
echo "Threads,Real Time,User Time,System Time" > "$output_file"

# Loop through 1 to 32 threads
for n in {1..32}; do
    echo "Running with $n threads..."

    # Set the number of Cilk workers
    export CILK_NWORKERS=$n

    # Run the program with the input file and capture time output
    time_output=$( { time ./othello-parallel < default_input; } 2>&1 )

    # Extract real, user, and sys times from the time output
    real_time=$(echo "$time_output" | grep "^real" | awk '{print $2}')
    user_time=$(echo "$time_output" | grep "^user" | awk '{print $2}')
    sys_time=$(echo "$time_output" | grep "^sys" | awk '{print $2}')

    # Append the results to the CSV output file
    echo "$n,$real_time,$user_time,$sys_time" >> "$output_file"

    echo "Threads: $n, Real: $real_time, User: $user_time, Sys: $sys_time"
done

echo "Execution times saved to $output_file"

