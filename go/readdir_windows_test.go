package main

import (
	"fmt"
	"syscall"
	"testing"
)

// TestHelloName calls greetings.Hello with a name, checking
// for a valid return value.
func TestHelloName(t *testing.T) {
	err := ReadDir("c:\\", func(finddata *syscall.Win32finddata) error {
		filename := syscall.UTF16ToString(finddata.FileName[:])
		fmt.Println(filename)
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
}
