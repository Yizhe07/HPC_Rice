#!/bin/bash
#SBATCH --export=ALL
#SBATCH --nodes=1 
#SBATCH --ntasks=1
#SBATCH --ntasks-per-node=1 
#SBATCH --cpus-per-task=16 
#SBATCH --mem-per-cpu=512
#SBATCH --threads-per-core=1
#SBATCH --time=00:30:00 
#SBATCH --partition=commons
#SBATCH --reservation=comp422

# --- Step 0: Compile both serial and parallel versions ---
echo "===== Compiling Both Serial and Parallel Versions ====="
icpc -O2 -cilk-serialize -o othello-serial othello.cpp
icpc -O2 -fcilkplus -o othello-parallel othello.cpp
echo "Compilation complete."
echo ""

# ---------------------------------------------
# Step 1: Baseline Sequential Execution
# ---------------------------------------------
echo "===== Running Baseline Sequential (Serialized) Version ====="
# Ensure that default_input contains:
#   c
#   7
#   c
#   7
baseline_output="serial_timing.txt"
echo "Baseline (serialized) run output will be saved to ${baseline_output}"
# Capture the timing output using the 'time' command.
baseline_time_output=$( { time ./othello-serial < default_input; } 2>&1 )
echo "$baseline_time_output" > ${baseline_output}
echo "Baseline run complete. Check ${baseline_output} for details."
echo ""

# Extract baseline timings (assumes GNU time output with lines starting with 'real', etc.)
baseline_real=$(echo "$baseline_time_output" | grep "^real" | awk '{print $2}')
baseline_user=$(echo "$baseline_time_output" | grep "^user" | awk '{print $2}')
baseline_sys=$(echo "$baseline_time_output" | grep "^sys" | awk '{print $2}')

# ---------------------------------------------
# Create CSV file and record baseline timing
# ---------------------------------------------
output_csv="execution_times.csv"
echo "Configuration,Real Time,User Time,System Time" > ${output_csv}
echo "Serial,${baseline_real},${baseline_user},${baseline_sys}" >> ${output_csv}
echo "Baseline timing recorded:"
echo "Serial, ${baseline_real}, ${baseline_user}, ${baseline_sys}"
echo ""

# ---------------------------------------------
# Step 2: Run Parallel Experiments (1-32 Threads)
# ---------------------------------------------
for n in {1..32}; do
    echo "===== Running with ${n} thread(s) ====="
    # Run the parallel version with CILK_NWORKERS set to n.
    time_output=$( { time CILK_NWORKERS=$n ./othello-parallel < default_input; } 2>&1 )
    
    # Extract the real, user, and sys times.
    real_time=$(echo "$time_output" | grep "^real" | awk '{print $2}')
    user_time=$(echo "$time_output" | grep "^user" | awk '{print $2}')
    sys_time=$(echo "$time_output" | grep "^sys" | awk '{print $2}')
    
    echo "$n,${real_time},${user_time},${sys_time}" >> ${output_csv}
    echo "Threads: $n, Real Time: $real_time, User Time: $user_time, Sys Time: $sys_time"
done

echo ""
echo "All runs complete. Execution times saved to ${output_csv}"



