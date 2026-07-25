.PHONY: all generate_input build build-rust build-cpp build-java build-csharp build-go build-kotlin build-haskell run plot open-plots latex latex-debug

all: build run plot 

# Run the input-generator program and write to input/{n}.in
generate_input:
	cd input-generator && cargo run -- -n 1000,3000,10000,30000,100000,300000,1000000,3000000,10000000 --output ../input

# TODO: Customize this for your language.
# Make sure to end with a `rmq` binary in the root directory.
build: build-cpp

build-cpp:
	g++ -std=c++17 -O3 -march=native rmq-cpp/*.cpp -o rmq

run:
	./rmq input > data.csv

plot:
	python3 plot.py

open-plots:
	open plots/*.png

report:
	cd report && latexmk -pvc -pdf -interaction=nonstopmode report.tex -cd -shell-escape
report-debug:
	cd report && latexmk -pdf report.tex -cd
