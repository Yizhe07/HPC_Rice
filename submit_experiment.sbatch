#!/bin/bash
#SBATCH --export=ALL
#SBATCH --nodes=1 
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem-per-cpu=512
#SBATCH --threads-per-core=1
#SBATCH --time=00:30:00
#SBATCH --partition=commons
#SBATCH --reservation=comp422

echo "===== Step 1: Running cilkscreen for Data Race Check ====="
#   c
#   4
#   c
#   4

cilkscreen ./othello-parallel < cilkscreen_input > cilkscreen.out 2>&1
echo "cilkscreen output saved to cilkscreen.out"
echo ""

echo "==== Running cilkview Profiling for Lookahead Depths 1-7 ===="
for depth in {1..7}; do
    # Create an input file for the current depth:
    input_file="default_input_depth_${depth}.txt"
    output_file="cilkview_depth_${depth}.out"
    
    cat <<EOF > "$input_file"
c
${depth}
c
${depth}
EOF

    echo "Running cilkview for lookahead depth ${depth}..."
    cilkview ./othello-parallel < "$input_file" > "$output_file" 2>&1
    echo "cilkview output for depth ${depth} saved to ${output_file}"
    echo ""
done

echo "==== All cilkscreen and cilkview experiments completed. ===="
