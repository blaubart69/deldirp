package main

import (
	"fmt"
	"os"
	"path/filepath"
	"sync"
)

// deleteFileOrDir removes a file or directory. If it's a directory, it will wait for all its content to be deleted first.
func deleteFileOrDir(path string, wg *sync.WaitGroup, errChan chan error) {
	defer wg.Done()

	// Check if it's a directory
	info, err := os.Stat(path)
	if err != nil {
		errChan <- fmt.Errorf("failed to stat: %v", err)
		return
	}

	// If it's a directory, walk through its contents
	if info.IsDir() {
		entries, err := os.ReadDir(path)
		if err != nil {
			errChan <- fmt.Errorf("failed to read directory: %v", err)
			return
		}

		// Create a new wait group for the directory's contents
		var contentWG sync.WaitGroup
		contentWG.Add(len(entries))

		// Recursively delete directory contents
		for _, entry := range entries {
			go func(entry os.DirEntry) {
				defer contentWG.Done()
				childPath := filepath.Join(path, entry.Name())
				deleteFileOrDir(childPath, &contentWG, errChan)
			}(entry)
		}

		// Wait for all contents to be deleted
		contentWG.Wait()
	}

	// Now delete the directory or file
	err = os.Remove(path)
	if err != nil {
		errChan <- fmt.Errorf("failed to remove: %v", err)
	} else {
		fmt.Println("Deleted:", path)
	}
}

func deleteDirectoryTree(root string) error {
	var wg sync.WaitGroup
	errChan := make(chan error, 1) // Error channel for any errors

	// Start the deletion process
	wg.Add(1)
	go deleteFileOrDir(root, &wg, errChan)

	// Wait for all deletions to complete
	wg.Wait()
	close(errChan)

	// Check if any errors occurred
	if len(errChan) > 0 {
		return <-errChan
	}
	return nil
}

func main_ChatGpt() {
	root := "./testdir" // Replace with your target directory

	err := deleteDirectoryTree(root)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
	} else {
		fmt.Println("Directory tree successfully deleted.")
	}
}
