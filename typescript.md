# Typescript

## Step1
### commands
* Disk Server
```
./BDS <DiskFileName> <#cylinders> <#sector per cylinder> <track-to-track delay> <port=10356>
```

* Disk Client
```
./BDC_command <DiskServerAddress> <port=10356>
./BDC_random <DiskServerAddress> <port=10356>
```


## Step2
### commands
* Disk Server

```
./BDS <DiskFileName> <#cylinders> <#sector per cylinder> <track-to-track delay> <port=10356>
```

* File System Server

```
./FS <DiskServerAddress> <BDSPort=10356> <FSPort=12356>
```
* File System Client

```
./FC <ServerAddr> <FSPort=12356>
```


## Step3

I implemented my step3 exactly in the files of step2,so I don't have a step3 folder,you just need to test step3 using the file system in step2.

