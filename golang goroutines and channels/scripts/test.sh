make
time {
    for file in scripts/tests/*; do
        if [ -f "$file" ]; then
            echo "Running test: $file" 
            time ./grader engine < $file
            echo ""
        fi
    done
}