# sensors-microcontrollers-26-27
Official repository for the sensors and microcontrollers subteam (2026-2027)

## Getting Started
There are a number of tools we use for the sake of development.
The following section will explain the purpose of each insofar as
this project and how to install them.

### Docker
We have virtual development environments for writing code for the 
Raspberry Pis on and off the car (with corresponding directory `onboard-rpi/`
and `houston/`, respectively).

#### Installation
**IF YOU USE WINDOWS MAKE SURE TO INSTALL WSL BEFORE YOU CONTINUE!**

1. Go to [https://docs.docker.com/desktop/](https://docs.docker.com/desktop/) and then in the navigation menu on the left look for Products->Docker Desktop->Setup->Install then click on the option for your device Windows/Mac/Linux then follow the instructions to install.

2. Open Docker Desktop and follow the account setup details.

#### Docker Usage with Github Repo
Always make sure to open Docker Desktop before trying to use Docker. This is because opening Docker Desktop starts the Docker Engine which powers all of Docker's features

#### Steps to Clone Repo and start the containers
1. git clone (Using HTTPS or SSH)
2. `cd sensors-microcontrollers-26-27`
3. `docker compose build`
4. `docker compose up -d`

#### Connecting to the containers
We have 2 containers that we will use for development houston-dev and onboard-rpi-dev and we have a few ways of accessing them once they are running

##### Via the Terminal:
Select which container to connect to and run the corresponding command:

  `docker exec -it houston-dev bash`

  `docker exec -it onboard-rpi-dev bash`

##### Using VSCode (preferred):
1. Open VSCode
2. Click on the little blue button in the bottom left corner
3. In the new menu that appears click on Attach to Running Container (if the necessary container extensions aren't installed VSCode will automatically install them)
4. If you ran docker compose build and docker compose up -d earlier then you should see two options houston-dev and onboard-rpi-dev
5. Click on whichever one you intend to work in

##### Navigating the Docker Containers:
When inside either of the 2 Docker Containers, you should do all of your project work in the `workspace` directory. To check which directory you are in, you can run the command `pwd`. To switch to the `workspace` directory, you can run the command `cd /workspace`. The `workspace` directory is the only one where any changes you make in the container will persist outside of the container. If you create files or write code in any of the other directories then it will be deleted when the container is shut down. This is because the workspace directory in both containers maps to the appropriate directory outside of the container.

houston container:     `workspace` ---> `houston`  
onboard-rpi container: `workspace` ---> `onboard-rpi`

#### How to shut down Docker Containers
Run this command in your terminal: `docker compose down`
