# Project Simple Mail Client

- Name: Lael Matthews
- Email: laelmatthews@u.boisestate.edu
- Class: CS525

## Known Bugs or Issues

There are no known bugs. Two branch paths were excluded due to system or library dependencies. One required a system socket() call and one required a library call to strdup().

## Experience

The two biggest eye openers for this project were how simple the SMTP protocol is at its core, and how much additional code is needed to implement the protocol. AI was used heavily during the project to understand various pieces such as establishing a socket, figuring out what libraries to include, how to structure code, and building test blocks. I was particularly impressed with the agent in code spaces. After my program was working, I asked it to generate tests and it achieved ~65% coverage in seconds. After some debugging together we had 100% coverage in about an hour. This would have probably taken me weeks to do by myself. Writing the code in a more testable manner was also unique and required re-writing a lot off the functions to separate out protocol, session, and transport tasks. 

## Analysis

The results of this project are simple. Either the client connects to the server or not. My most recent message was successful and used this command: 
echo "I hear Vienna is nice this time of year." | ./build/release/myapp -f DA@boisestate.edu -t SG@uofI.edu -s "Vacation" -H o
nyx.boisestate.edu ec2-54-148-3-55.us-west-2.compute.amazonaws.com
While building the various tests it was interesting to see how many of the branches could be tested by breaking up normal calls like recv() and send() using call back abstraction.


