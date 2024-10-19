package deldirp

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"runtime"
)

func main() {
	workers := flag.Int("w", runtime.NumCPU(), "number of workers")
	flag.Parse()

	if len(flag.Args()) != 1 {
		fmt.Fprintf(os.Stderr, "Usage of %s: [OPTS] {directory}\n", filepath.Base(os.Args[0]))
		flag.PrintDefaults()
		os.Exit(4)
	}

	cleanPath, err := filepath.Abs(flag.Arg(0))
	if err != nil {
		log.Fatal(err)
	}

	cleanPathStat, err := os.Stat(cleanPath)
	if err != nil {
		log.Fatal(err)
	}

	if !cleanPathStat.IsDir() {
		log.Fatalf("given argument is not a directory. (%s)\n", cleanPath)
	}

}
