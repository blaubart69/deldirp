package main

import "syscall"

func ReadDir(path string, onEntry func(*syscall.Win32finddata) error) error {

	var finddata syscall.Win32finddata

	if utf16path, err := syscall.UTF16PtrFromString(path); err != nil {
		return err
	} else if findhandle, err := syscall.FindFirstFile(utf16path, &finddata); err != nil {
		return err
	} else {
		for {
			onEntry(&finddata)
			if err = syscall.FindNextFile(findhandle, &finddata); err != nil {
				if err == syscall.ERROR_NO_MORE_FILES {
					return nil
				} else {
					return err
				}
			}
		}
	}
}
