.PHONY: all generate_input build build-rust build-cpp build-java build-csharp build-go build-kotlin build-haskell run plot open-plots latex latex-debug

all: build run plot 

# Run the input-generator program and write to input/{n}.in
generate_input:
	cd input-generator && cargo run -- -n 1000,3000,10000,30000,100000,300000,1000000,3000000,10000000 --output ../input

# TODO: Customize this for your language.
# Make sure to end with a `rmq` binary in the root directory.
build: build-rust

build-rust:
	cd rmq-rust && cargo build --release
	cp rmq-rust/target/release/rmq-rust rmq

build-cpp:
	g++ -std=c++17 -O3 -march=native rmq-cpp/*.cpp -o rmq

build-java:
	javac -d rmq-java/build rmq-java/Rmq.java
	printf '#!/usr/bin/env sh\nexec java -cp "$(dirname "$$0")/rmq-java/build" Main "$$@"\n' > rmq
	chmod +x rmq

build-csharp:
	mcs -optimize+ -out:rmq-csharp/rmq-csharp.exe rmq-csharp/Program.cs
	printf '#!/usr/bin/env sh\nexec mono "$(dirname "$$0")/rmq-csharp/rmq-csharp.exe" "$$@"\n' > rmq
	chmod +x rmq

build-go:
	cd rmq-go && go build -o rmq-go .
	cp rmq-go/rmq-go rmq

build-kotlin:
	kotlinc rmq-kotlin/Rmq.kt -include-runtime -d rmq-kotlin/rmq-kotlin.jar
	printf '#!/usr/bin/env sh\nexec java -jar "$(dirname "$$0")/rmq-kotlin/rmq-kotlin.jar" "$$@"\n' > rmq
	chmod +x rmq

build-haskell:
	ghc -O3 -package vector -package time -package directory rmq-haskell/Rmq.hs -o rmq-haskell/rmq-haskell
	cp rmq-haskell/rmq-haskell rmq

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
