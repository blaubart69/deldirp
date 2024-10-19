package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"sync"
	"sync/atomic"
)

type Stats struct {
	files  uint64
	dirs   uint64
	errors uint64
}

type Dir struct {
	parent *Dir
	ref    int64
	name   string
}

type File struct {
	dir  *Dir
	name string
}

type Queue struct {
	itemCount int64
	itemsChan chan interface{}
}

func (q *Queue) enqueue(item interface{}) {
	atomic.AddInt64(&q.itemCount, 1)
	q.itemsChan <- item
}

func RemoveFile(filename string, stats *Stats) {
	if err := os.Remove(filename); err != nil {
		log.Printf("E: remove file: %v\n", err)
		atomic.AddUint64(&stats.errors, 1)
	} else {
		log.Printf("D: remove file: %s\n", filename)
		atomic.AddUint64(&stats.files, 1)
	}
}

func EnqueueDirItems(d *Dir, queue *Queue, stats *Stats) {
	entries, err := os.ReadDir(d.name)
	if err != nil {
		atomic.AddUint64(&stats.errors, 1)
		log.Printf("E: ReadDir() %v\n", err)
		return
	}

	for _, entry := range entries {
		atomic.AddInt64(&d.ref, 1)
		fullName := filepath.Join(d.name, entry.Name())
		if entry.IsDir() {
			queue.enqueue(&Dir{parent: d, ref: 1, name: fullName})
		} else {
			queue.enqueue(&File{dir: d, name: fullName})
		}
	}
}

func deldir(queue *Queue, stats *Stats, wg *sync.WaitGroup) {
	defer wg.Done()

	for item := range queue.itemsChan {
		var currDir *Dir
		switch item.(type) {
		case *File:
			f := item.(*File)
			RemoveFile(f.name, stats)
			currDir = f.dir
		case *Dir:
			d := item.(*Dir)
			EnqueueDirItems(d, queue, stats)
			currDir = d
		}

		for {
			if currDir == nil {
				break
			} else {
				// are we the last to turn off the light?
				if atomic.AddInt64(&currDir.ref, -1) == 0 {
					if err := os.Remove(currDir.name); err != nil {
						log.Printf("E: directory: %v\n", err)
					} else {
						atomic.AddUint64(&stats.dirs, 1)
						log.Printf("D: remove directory: %s\n", currDir.name)
						currDir = currDir.parent
					}
				} else {
					break
					// if > 0 ... there are some refs lefts to this dir
					// if < 0 ... someone else has reached 0 first
					//				and is doing the cleanup
				}
			}
		}

		if atomic.AddInt64(&queue.itemCount, -1) == 0 {
			close(queue.itemsChan)
		}
	}
}

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

	var stats Stats
	var wg sync.WaitGroup
	queue := Queue{
		itemCount: 0,
		itemsChan: make(chan interface{}, 4096),
	}
	for i := 0; i < *workers; i++ {
		wg.Add(1)
		go deldir(&queue, &stats, &wg)
	}

	queue.enqueue(&Dir{
		parent: nil,
		ref:    1,
		name:   cleanPath,
	})

	wg.Wait()
}
