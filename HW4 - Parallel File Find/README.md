# Parallel File Find - Multi-Threading and Filesystem

## Description
Implementation of file lookup in a directory, utilizing threads to make the search parallel.  

## Files
* **pfind.c:** Given 3 arguments (Search root directory, Search term, Searching thread amount), searches in the 
  root directory for the search term while managing a queue used "mutexly" by the threads.