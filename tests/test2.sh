#!/bin/bash
echo "Hello TEST!!"

for i in {1..10}
do
  echo -en "$i/10\t \r"
  sleep 1
done
echo "Done"

echo "My PID: " $$

# fullname="USER INPUT"
read -p "Enter fullname: " fullname
# user="USER INPUT"
read -p "Enter user: " user


echo "Fullname: $fullname"
echo "User: $user"

echo "Thank You $fullname for using our service!"