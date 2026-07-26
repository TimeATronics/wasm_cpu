struct cpu {
  struct proc *proc;
  struct context *scheduler;  /* swtch() here to enter scheduler */
  int noff;
  int intena;
};
