define file name=bigfile,path=bigfile,size=1g,prealloc,reuse
define process name=randomizer
{
  thread random-thread procname=randomizer
  {
    flowop read name=random-read,filename=bigfile,iosize=16k,random
  }
}
