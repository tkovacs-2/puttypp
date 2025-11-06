# msvcrt.dll 7.0.26100.7019
set $mallocend = 226
set $reallocend = 915
# msvcrt.dll 7.0.19041.3636
#set $mallocend = 233
#set $reallocend = 899

set pagination off
set breakpoint pending on
set $allocs = 0
break *(malloc+$mallocend)
commands
  silent
  set $allocs = $allocs+1
  set $caller = *(unsigned long *)($esp)
  set $r1 = realloc
  set $r2 =  realloc+$reallocend
  if ($caller < $r1 || $caller > $r2) && $eax != 0
    printf "0x%08lx alloc %d\n", $eax, $allocs
    bt
  end
  continue
end
break free
commands
  silent
  set $address = *(unsigned long *)($esp+4)
  if $address != 0
    printf "0x%08lx free\n", $address
  end
  continue
end
break realloc
commands
  silent
  set $address = *(unsigned long *)($esp+4)
  if $address != 0
    printf "0x%08lx free\n", $address
  end
  continue
end
break *(realloc+$reallocend)
commands
  silent
  set $allocs = $allocs+1
  if $eax != 0
    printf "0x%08lx alloc %d\n", $eax, $allocs
    bt
  end
  continue
end
set logging overwrite on
set logging file memleak.out
set logging redirect on
set logging enabled on
run
quit
