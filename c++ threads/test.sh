make
for file in scripts/*; do
    if [ -f "$file" ]; then
        echo "Running test: $file" 
        ./grader engine < $file
        echo ""
    fi
done