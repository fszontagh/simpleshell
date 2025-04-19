# Test Cases

 This document lists manual test cases to exercise the newly enhanced
 job control (`jobs`, `bg`, `fg`) and pipeline handling features.

 ## 1. Testing the `jobs` builtin

 Use these commands in the shell to verify background/foreground job control:

 ```bash
 # 1. Start a simple background job
 sleep 5 &

 # 2. List current background jobs
 jobs

 # 3. Start a foreground job and suspend it with Ctrl-Z
 sleep 5
 # (Press Ctrl-Z)

 # 4. Verify the stopped job appears in the stopped list
 jobs

 # 5. Resume the last stopped job in the background
 bg
 jobs

 # 6. Bring the last stopped job to the foreground
 fg
 jobs

 # 7. Use explicit job numbers: start two jobs
 sleep 5 &
 sleep 6 &
 jobs

 # 8. Resume job 2 in background and job 1 in foreground
 bg %2
 jobs
 fg %1
 jobs
 ```

 ## 2. Testing pipelines

 Verify that pipelines with or without redirections behave correctly:

 ```bash
 # 1. Simple filter and sort pipeline
 echo -e "orange\napple\nbanana" | grep a | sort

 # 2. Count lines in this document via a pipeline and redirection
 cat docs/TESTCASES.md | wc -l > line_count.txt
 cat line_count.txt

 # 3. Pipeline with sleep and cat in background
 sleep 3 | cat &
 jobs

 # 4. Chained pipelines with tee and file redirection
 seq 1 10 | tee numbers.txt | grep 5 > found.txt
 cat found.txt
 ```