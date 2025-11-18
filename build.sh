
echo "Building ALang Engine..."
g++ -std=c++17 -O2 ALangEngine.cpp Main.cpp -o alang -fexec-charset=GBK
echo "Build completed."