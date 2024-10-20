using System;
using System.IO;
using System.Threading.Tasks;

class Program
{
    static async Task DeleteFileOrDir(string path)
    {
        try
        {
            // Check if it's a directory
            if (Directory.Exists(path))
            {
                // Get all files and directories inside this directory
                var files = Directory.GetFiles(path);
                var directories = Directory.GetDirectories(path);

                // Delete all files in parallel
                var fileDeleteTasks = new Task[files.Length];
                for (int i = 0; i < files.Length; i++)
                {
                    var file = files[i];
                    fileDeleteTasks[i] = Task.Run(() => File.Delete(file));
                }

                // Wait for all file deletions to complete
                await Task.WhenAll(fileDeleteTasks);

                // Delete all directories recursively in parallel
                var directoryDeleteTasks = new Task[directories.Length];
                for (int i = 0; i < directories.Length; i++)
                {
                    var dir = directories[i];
                    directoryDeleteTasks[i] = DeleteFileOrDir(dir); // Recursive delete
                }

                // Wait for all directory deletions to complete
                await Task.WhenAll(directoryDeleteTasks);

                // Finally delete the directory itself
                Directory.Delete(path);
                Console.WriteLine("Deleted directory: " + path);
            }
            else if (File.Exists(path))
            {
                // If it's a file, just delete it
                File.Delete(path);
                Console.WriteLine("Deleted file: " + path);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error deleting {path}: {ex.Message}");
        }
    }

    static async Task DeleteDirectoryTree(string root)
    {
        // Start deletion of the directory tree
        await DeleteFileOrDir(root);
    }

    static async Task Main(string[] args)
    {
        string rootDir = "./testdir";  // Replace with your target directory

        try
        {
            // Call the method to delete the directory tree
            await DeleteDirectoryTree(rootDir);
            Console.WriteLine("Directory tree successfully deleted.");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error: {ex.Message}");
        }
    }
}
