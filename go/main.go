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
	"time"
)

const CacheLinePadSize = 64

type CacheLinePad struct {
	_ [CacheLinePadSize]byte
}

type Stats struct {
	files  uint64
	_      CacheLinePad
	dirs   uint64
	_      CacheLinePad
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
		//log.Printf("D: remove file: %s\n", filename)
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

func EnqueueTopLevelItems(dirname string, queue *Queue, stats *Stats) {
	entries, err := os.ReadDir(dirname)
	if err != nil {
		atomic.AddUint64(&stats.errors, 1)
		log.Printf("E: ReadDir() %v\n", err)
		return
	}

	for _, entry := range entries {
		fullName := filepath.Join(dirname, entry.Name())
		if entry.IsDir() {
			queue.enqueue(&Dir{parent: nil, ref: 1, name: fullName})
		} else {
			queue.enqueue(&File{dir: nil, name: fullName})
		}
	}
}

func removeDirsUpwardsWithRefCountZero(currDir *Dir, stats *Stats) {
	for {
		if currDir == nil {
			break
		} else {
			// are we the last to turn off the light?
			// 	ether we removed a file or enumerated a directory.
			//		file: removed one file
			//			==> decrement by 1
			//		dir:  processed this directory (initial refcount was 1)
			//          ==> we are done with this dir and decrement by 1
			if atomic.AddInt64(&currDir.ref, -1) == 0 {
				if err := os.Remove(currDir.name); err != nil {
					log.Printf("E: remove directory: %v\n", err)
				} else {
					atomic.AddUint64(&stats.dirs, 1)
					// important!
					// 	 we have to check the directories all the way up
					currDir = currDir.parent
				}
			} else {
				break
				// if > 0 ... there are some refs lefts to this dir.
				//			  	someone will come accross here later
				//				and remove the directory.
				// if < 0 ... someone else has reached 0 first
				//				and is doing the cleanup.
			}
		}
	}
}

func deldir(queue *Queue, stats *Stats, wg *sync.WaitGroup) {
	defer wg.Done()

	for item := range queue.itemsChan {
		var currDir *Dir
		switch item := item.(type) {
		case *File:
			RemoveFile(item.name, stats)
			currDir = item.dir
		case *Dir:
			EnqueueDirItems(item, queue, stats)
			currDir = item
		}

		removeDirsUpwardsWithRefCountZero(currDir, stats)

		if atomic.AddInt64(&queue.itemCount, -1) == 0 {
			// when itemCount reaches zero, no other worker is active anymore.
			// close the channel and signal "end" to other workers
			close(queue.itemsChan)
		}
	} // channel loop
}

func PrintStats(stats *Stats, queue chan interface{}) {
	fmt.Printf("queued: %12d dirs: %12d files: %12d errors: %12d\n",
		len(queue),
		atomic.LoadUint64(&stats.dirs),
		atomic.LoadUint64(&stats.files),
		atomic.LoadUint64(&stats.errors))
}

func main() {
	workers := flag.Int("w", runtime.NumCPU(), "number of workers")
	optEmptyDir := flag.Bool("e", false, "empty directory. Do not remove itself.")
	flag.Parse()

	if len(flag.Args()) != 1 {
		fmt.Fprintf(os.Stderr, "Usage of %s: [OPTS] {directory}\n", filepath.Base(os.Args[0]))
		flag.PrintDefaults()
		os.Exit(4)
	}

	cleanPath, err := filepath.Abs(flag.Arg(0))
	if err != nil {
		log.Fatal(err)
		os.Exit(4)
	}

	cleanPathStat, err := os.Stat(cleanPath)
	if err != nil {
		log.Fatal(err)
		os.Exit(4)
	}

	if !cleanPathStat.IsDir() {
		log.Fatalf("given argument is not a directory. (%s)\n", cleanPath)
		os.Exit(4)
	}

	var stats Stats
	var wg sync.WaitGroup
	queue := Queue{
		itemCount: 0,
		itemsChan: make(chan interface{}, 512*1024),
	}
	for i := 0; i < *workers; i++ {
		wg.Add(1)
		go deldir(&queue, &stats, &wg)
	}

	go func(stats *Stats, queue chan interface{}) {
		for {
			time.Sleep(2 * time.Second)
			PrintStats(stats, queue)
		}
	}(&stats, queue.itemsChan)

	if *optEmptyDir {
		queue.itemCount += 1
		EnqueueTopLevelItems(cleanPath, &queue, &stats)
		if atomic.AddInt64(&queue.itemCount, -1) == 0 {
			// this means that:
			// all items were already deleted while enqueuing them here
			close(queue.itemsChan)
		}
	} else {
		queue.enqueue(&Dir{
			parent: nil,
			ref:    1,
			name:   cleanPath,
		})
	}

	wg.Wait()

	PrintStats(&stats, queue.itemsChan)
}
