// ==================================================
// Decompiled: FUN_0065f2d8 at 0x65f2d8
// ==================================================


void FUN_0065f2d8(long *param_1,undefined8 *param_2)

{
  byte bVar1;
  byte bVar2;
  undefined4 uVar3;
  undefined4 uVar4;
  undefined4 uVar5;
  undefined **__ptr;
  undefined **ppuVar6;
  char cVar7;
  int iVar8;
  undefined8 *puVar9;
  undefined8 *puVar10;
  void *pvVar11;
  long lVar12;
  ulong *puVar13;
  undefined *puVar14;
  undefined **ppuVar15;
  ulong uVar16;
  undefined *puVar17;
  ulong uVar18;
  undefined8 uVar19;
  ulong uVar20;
  uint *puVar21;
  long lVar22;
  long *plVar23;
  undefined ***pppuVar24;
  ulong uVar25;
  ulong *puVar26;
  ulong uVar27;
  bool bVar28;
  bool bVar29;
  undefined **local_108;
  ulong uStack_100;
  long *local_f8;
  undefined **local_e8;
  undefined **ppuStack_e0;
  long *local_d8;
  long *local_d0;
  undefined **local_c8;
  ulong uStack_c0;
  long *local_b8;
  long *local_b0;
  uint local_a8;
  undefined4 uStack_a4;
  undefined8 local_a0;
  long *local_98;
  long *local_90;
  undefined **local_88;
  undefined **local_80;
  long local_78;
  long local_70;
  ulong local_68;
  void *local_60;
  long local_58;
  undefined1 local_48 [16];
  long *local_38;
  
  *param_1 = 0;
  param_1[2] = 0;
  param_1[3] = 0;
  param_1[1] = (long)&PTR_FUN_00a11260;
  *(undefined1 *)(param_1 + 4) = 0;
  param_1[5] = 0;
  param_1[6] = (long)&PTR_FUN_00a112b0;
  param_1[7] = 0;
  param_1[8] = 0;
  param_1[9] = 0;
  param_1[10] = 0;
  *(undefined1 *)(param_1 + 0xb) = 0;
  param_1[0xc] = (long)&PTR_FUN_00a112b0;
  param_1[0xd] = 0;
  param_1[0xe] = 0;
  param_1[0x10] = 0;
  param_1[0x11] = 0;
  param_1[0xf] = (long)&PTR_FUN_00a11260;
  *(undefined1 *)(param_1 + 0x12) = 0;
  param_1[0x13] = 0;
  param_1[0x14] = 0;
  param_1[0x15] = 0;
  param_1[0x16] = 0;
  param_1[0x17] = 0;
  param_1[0x18] = 0;
  param_1[0x19] = 0;
  param_1[0x1a] = 0;
  param_1[0x1b] = 0;
  *(undefined1 *)(param_1 + 0x1c) = 1;
  *(undefined1 *)(param_1 + 0x1d) = 1;
  *(undefined4 *)(param_1 + 0x1e) = 0;
  param_1[0x21] = 0;
  param_1[0x22] = 0;
  param_1[0x1f] = (long)&DAT_001ee318;
  param_1[0x20] = 1;
  *(undefined2 *)((long)param_1 + 0x10c) = 0xff00;
  *(undefined4 *)(param_1 + 0x21) = 1;
  uVar3 = *(undefined4 *)((long)param_2 + 4);
  uVar4 = *(undefined4 *)(param_2 + 1);
  uVar5 = *(undefined4 *)((long)param_2 + 0xc);
  *(undefined4 *)(param_1 + 0x23) = *(undefined4 *)param_2;
  *(undefined4 *)((long)param_1 + 0x11c) = uVar3;
  *(undefined4 *)(param_1 + 0x24) = uVar4;
  *(undefined4 *)((long)param_1 + 0x124) = uVar5;
  plVar23 = (long *)param_2[2];
  param_1[0x25] = (long)plVar23;
  if (plVar23 != (long *)0x0) {
    (**(code **)(*plVar23 + 0x10))();
  }
  local_90 = param_1 + 0x14;
  local_d0 = param_1 + 0x1f;
                    /* try { // try from 0065f3f1 to 0065f403 has its CatchHandler @ 0066006a */
  cVar7 = FUN_00642388();
  if (cVar7 == '\0') {
                    /* try { // try from 0065f42c to 0065f435 has its CatchHandler @ 0066006a */
    puVar9 = (undefined8 *)FUN_00935ae9(0x10);
    *puVar9 = 0;
    puVar9[1] = 0;
                    /* try { // try from 0065f43f to 0065f448 has its CatchHandler @ 0065ffe9 */
    puVar10 = (undefined8 *)FUN_00935ae9(8);
    *puVar10 = &PTR_FUN_00a11308;
    *puVar9 = puVar10;
                    /* try { // try from 0065f459 to 0065f462 has its CatchHandler @ 0065ffcf */
    plVar23 = (long *)FUN_00935ae9(0x10);
    LOCK();
    UNLOCK();
    lVar22 = DAT_00a4c4f0 + 1;
    *plVar23 = DAT_00a4c4f0;
    DAT_00a4c4f0 = lVar22;
    plVar23[1] = (long)FUN_0065f228;
    puVar9[1] = plVar23;
  }
  else {
    puVar9 = (undefined8 *)FUN_00935ae9(0x20);
    puVar9[2] = 0;
    puVar9[3] = 0;
    *puVar9 = 0;
    puVar9[1] = 0;
                    /* try { // try from 0065f411 to 0065f418 has its CatchHandler @ 0065ffeb */
    FUN_003b14ec(puVar9);
                    /* try { // try from 0065f425 to 0065f429 has its CatchHandler @ 0065ffdc */
    FUN_0065f266(puVar9 + 1);
  }
  param_1[0x26] = (long)puVar9;
  *(uint *)(param_1 + 0x27) = (uint)(cVar7 != '\0');
                    /* try { // try from 0065f497 to 0065f49b has its CatchHandler @ 00660062 */
  FUN_006614c9();
  if ((*(byte *)(DAT_00a63528 + 8) & 1) == 0) {
    uVar16 = (ulong)(*(byte *)(DAT_00a63528 + 8) >> 1);
  }
  else {
    uVar16 = *(ulong *)(DAT_00a63528 + 0x10);
  }
                    /* try { // try from 0065f4bb to 0065f4c7 has its CatchHandler @ 00660060 */
  FUN_003cb63a(&local_60,uVar16 + 4);
  pvVar11 = local_60;
  uVar27 = local_58 - (long)local_60;
  if ((ulong)param_2[1] < uVar27) {
    puVar9 = (undefined8 *)FUN_003689a8(0x10);
    local_e8 = (undefined **)0x163efc;
    ppuStack_e0 = (undefined **)0x30;
    local_d8 = (long *)CONCAT44(local_d8._4_4_,0x6e);
    local_108 = (undefined **)0x0;
    uStack_100 = 0;
                    /* try { // try from 0065fd13 to 0065fd17 has its CatchHandler @ 00660205 */
    FUN_0039e2b0();
    local_108 = &PTR_FUN_00a3e1d0;
                    /* try { // try from 0065fd27 to 0065fd3a has its CatchHandler @ 00660104 */
    FUN_00315621(&local_e8);
    FUN_006602ad(&local_108);
                    /* try { // try from 0065fd3d to 0065fd87 has its CatchHandler @ 006600bf */
    FUN_0032ea2e(&local_108,uVar27);
    FUN_003a149d(&local_108," bytes");
    puVar9[1] = uStack_100;
    uStack_100 = 0;
    *puVar9 = &PTR_FUN_00a3e1d0;
                    /* WARNING: Subroutine does not return */
    FUN_00368ab9(puVar9,&PTR_PTR_00a3e130,FUN_00315706);
  }
  memcpy(local_60,(void *)*param_2,uVar27);
  if (uVar16 == 0) {
    local_b0 = &DAT_001f0700;
  }
  else {
                    /* try { // try from 0065f501 to 0065f510 has its CatchHandler @ 00660032 */
    FUN_003164c6(&local_b0,pvVar11,uVar16);
  }
                    /* try { // try from 0065f518 to 0065f51c has its CatchHandler @ 00660058 */
  FUN_006614c9();
  cVar7 = FUN_0031f2c4(DAT_00a63528,local_b0);
  if (cVar7 == '\0') {
    puVar9 = (undefined8 *)FUN_003689a8(0x10);
    local_e8 = (undefined **)0x163efc;
    ppuStack_e0 = (undefined **)0x30;
    local_d8 = (long *)CONCAT44(local_d8._4_4_,0x75);
    local_108 = (undefined **)0x0;
    uStack_100 = 0;
                    /* try { // try from 0065fdc2 to 0065fdc6 has its CatchHandler @ 006601f2 */
    FUN_0039e2b0();
    local_108 = &PTR_FUN_00a3e1d0;
                    /* try { // try from 0065fdd6 to 0065fdf0 has its CatchHandler @ 006600c4 */
    FUN_00315621(&local_e8);
    FUN_003a149d(&local_108,"Invalid stream start: expected ");
                    /* try { // try from 0065fdf1 to 0065fe06 has its CatchHandler @ 006600bd */
    FUN_006614c9();
    FUN_0032ec24(&local_a8,&DAT_00a63528);
                    /* try { // try from 0065fe0c to 0065fe26 has its CatchHandler @ 0066008d */
    FUN_003156bb(&local_108,CONCAT44(uStack_a4,local_a8));
    FUN_003a149d(&local_108,", got ");
                    /* try { // try from 0065fe27 to 0065fe35 has its CatchHandler @ 00660086 */
    FUN_0032ec24(&local_c8,&local_b0);
                    /* try { // try from 0065fe3d to 0065fe74 has its CatchHandler @ 00660077 */
    FUN_003156bb(&local_108,local_c8);
    puVar9[1] = 0;
    puVar9[1] = uStack_100;
    uStack_100 = 0;
    *puVar9 = &PTR_FUN_00a3e1d0;
                    /* WARNING: Subroutine does not return */
    FUN_00368ab9(puVar9,&PTR_PTR_00a3e130,FUN_00315706);
  }
  iVar8 = *(int *)((long)pvVar11 + uVar16);
  if (iVar8 != 1) {
    puVar9 = (undefined8 *)FUN_003689a8(0x10);
    local_e8 = (undefined **)0x163efc;
    ppuStack_e0 = (undefined **)0x30;
    local_d8 = (long *)CONCAT44(local_d8._4_4_,0x7c);
    local_108 = (undefined **)0x0;
    uStack_100 = 0;
                    /* try { // try from 0065feaf to 0065feb3 has its CatchHandler @ 006601df */
    FUN_0039e2b0();
    local_108 = &PTR_FUN_00a3e1d0;
                    /* try { // try from 0065fec5 to 0065ff38 has its CatchHandler @ 006601a8 */
    FUN_00315621(&local_e8);
    FUN_003a149d(&local_108,"Invalid trie version: expected ");
    FUN_006602fe(&local_108,1);
    FUN_003a149d(&local_108,", got ");
    FUN_006602fe(&local_108,iVar8);
    puVar9[1] = uStack_100;
    uStack_100 = 0;
    *puVar9 = &PTR_FUN_00a3e1d0;
                    /* WARNING: Subroutine does not return */
    FUN_00368ab9(puVar9,&PTR_PTR_00a3e130,FUN_00315706);
  }
                    /* try { // try from 0065f549 to 0065f558 has its CatchHandler @ 00660056 */
  FUN_003a1310(&local_c8,param_2,uVar27,param_2[1]);
                    /* try { // try from 0065f559 to 0065f562 has its CatchHandler @ 00660051 */
  pvVar11 = (void *)FUN_00935ae9(400);
  memset(pvVar11,0,400);
  FUN_00660364(pvVar11);
  if ((void *)*param_1 != pvVar11) {
    if ((void *)*param_1 != (void *)0x0) {
      FUN_0066042e();
    }
    *param_1 = (long)pvVar11;
  }
                    /* try { // try from 0065f59c to 0065f5ad has its CatchHandler @ 0066004c */
  FUN_00660496(&local_e8,pvVar11,&local_c8);
  plVar23 = local_b8;
  local_b8 = local_d8;
  uStack_c0 = (ulong)ppuStack_e0;
  local_c8 = local_e8;
  local_e8 = (undefined **)0x0;
  ppuStack_e0 = (undefined **)0x0;
  local_d8 = (long *)0x0;
  if ((plVar23 != (long *)0x0) && ((**(code **)(*plVar23 + 0x18))(), local_d8 != (long *)0x0)) {
    (**(code **)(*local_d8 + 0x18))();
  }
  param_1[7] = (long)*local_c8;
  puVar17 = local_c8[1];
  param_1[8] = (long)(local_c8 + 2);
                    /* try { // try from 0065f61f to 0065f62b has its CatchHandler @ 00660047 */
  puVar21 = &local_a8;
  FUN_003a1310(puVar21,&local_c8,(long)puVar17 * 8 + 0x10,uStack_c0);
  plVar23 = (long *)CONCAT44(uStack_a4,local_a8);
  lVar22 = *plVar23;
  param_1[9] = lVar22;
  param_1[10] = plVar23[1];
  if (0xff < (ulong)plVar23[2]) {
    puVar9 = (undefined8 *)FUN_003689a8(0x10);
    local_e8 = (undefined **)0x1535c2;
    ppuStack_e0 = (undefined **)0x2f;
    local_d8 = (long *)CONCAT44(local_d8._4_4_,0xf8);
    local_108 = (undefined **)0x0;
    uStack_100 = 0;
                    /* try { // try from 0065ff6e to 0065ff72 has its CatchHandler @ 0066017b */
    FUN_0039e2b0();
    local_108 = &PTR_FUN_00a3e1d0;
                    /* try { // try from 0065ff84 to 0065ffc4 has its CatchHandler @ 00660144 */
    FUN_00315621(&local_e8);
    FUN_00660bad(&local_108);
    puVar9[1] = uStack_100;
    uStack_100 = 0;
    *puVar9 = &PTR_FUN_00a3e1d0;
                    /* WARNING: Subroutine does not return */
    FUN_00368ab9(puVar9,&PTR_PTR_00a3e130,FUN_00315706);
  }
  *(char *)(param_1 + 0xb) = (char)plVar23[2];
  if (lVar22 == 0) {
                    /* try { // try from 0065f723 to 0065f734 has its CatchHandler @ 0066003f */
    FUN_003a1310(local_48,&local_a8,0x18,local_a0);
  }
  else {
                    /* try { // try from 0065f666 to 0065f674 has its CatchHandler @ 0066003f */
    FUN_003a1310(&local_108,&local_a8,0x18,local_a0);
    param_1[0xd] = (long)*local_108;
    puVar17 = local_108[1];
    param_1[0xe] = (long)(local_108 + 2);
                    /* try { // try from 0065f6a1 to 0065f6aa has its CatchHandler @ 00660016 */
    FUN_003a1310(&local_e8,&local_108,(long)puVar17 * 8 + 0x10,uStack_100);
    if (local_f8 != (long *)0x0) {
      (**(code **)(*local_f8 + 0x18))();
    }
                    /* try { // try from 0065f6bf to 0065f6d0 has its CatchHandler @ 0065fffd */
    FUN_0066084e(&local_108,param_1 + 0xf,&local_e8);
    plVar23 = local_d8;
    local_e8 = local_108;
    ppuStack_e0 = (undefined **)uStack_100;
    local_d8 = local_f8;
    if (plVar23 != (long *)0x0) {
      (**(code **)(*plVar23 + 0x18))();
    }
                    /* try { // try from 0065f6f5 to 0065f70e has its CatchHandler @ 0065fff8 */
    FUN_00661806(local_48,local_90,&local_e8);
    if (local_f8 != (long *)0x0) {
      (**(code **)(*local_f8 + 0x18))(local_f8);
    }
  }
  if (local_98 != (long *)0x0) {
    (**(code **)(*local_98 + 0x18))();
  }
  plVar23 = local_b8;
  local_b8 = local_38;
  if (plVar23 != (long *)0x0) {
    (**(code **)(*plVar23 + 0x18))();
  }
                    /* try { // try from 0065f77b to 0065f78c has its CatchHandler @ 0066003a */
  FUN_0066084e(&local_e8,param_1 + 1,&local_c8);
  plVar23 = local_b8;
  local_c8 = local_e8;
  uStack_c0 = (ulong)ppuStack_e0;
  local_b8 = local_d8;
  if (plVar23 != (long *)0x0) {
    (**(code **)(*plVar23 + 0x18))();
  }
  local_e8 = &PTR_FUN_009acdf8;
  lVar22 = 4;
  plVar23 = param_1 + 0x1e;
  ppuStack_e0 = local_c8;
  local_d8 = (long *)uStack_c0;
  do {
    if (lVar22 == 0) break;
                    /* try { // try from 0065f802 to 0065f80d has its CatchHandler @ 006600a3 */
    lVar12 = (*(code *)local_e8[2])(&local_e8,plVar23,lVar22);
    plVar23 = (long *)((long)plVar23 + lVar12);
    lVar22 = lVar22 - lVar12;
  } while (lVar12 != 0);
  lVar22 = (long)plVar23 - (long)(param_1 + 0x1e);
  if (lVar22 == 4) {
    lVar22 = 4;
    do {
      if (lVar22 == 0) break;
                    /* try { // try from 0065f83b to 0065f846 has its CatchHandler @ 0066009e */
      lVar12 = (*(code *)local_e8[2])(&local_e8,puVar21,lVar22);
      puVar21 = (uint *)((long)puVar21 + lVar12);
      lVar22 = lVar22 - lVar12;
    } while (lVar12 != 0);
    lVar22 = (long)puVar21 - (long)&local_a8;
    if (lVar22 == 4) {
      local_88 = (undefined **)(ulong)local_a8;
      if ((undefined **)(ulong)local_a8 == (undefined **)0xffffffff) {
        lVar22 = 8;
        pppuVar24 = &local_108;
        do {
          if (lVar22 == 0) break;
                    /* try { // try from 0065f88f to 0065f89a has its CatchHandler @ 00660088 */
          lVar12 = (*(code *)local_e8[2])(&local_e8,pppuVar24,lVar22);
          pppuVar24 = (undefined ***)((long)pppuVar24 + lVar12);
          lVar22 = lVar22 - lVar12;
        } while (lVar12 != 0);
        lVar22 = (long)pppuVar24 - (long)&local_108;
        local_88 = local_108;
        if (lVar22 != 8) {
          uVar19 = 8;
          goto LAB_0065fcca;
        }
      }
      if (param_1[0x22] != 0) {
        uVar16 = (ulong)*(uint *)(param_1 + 0x21) + param_1[0x22] * 2;
        uVar25 = uVar16 / 3;
        FUN_00660a4a(local_d0);
        uVar27 = (ulong)*(uint *)(param_1 + 0x21);
        if (uVar25 < uVar27) {
          if (uVar16 < 0x18) {
            uVar16 = 0x200000007;
            lVar22 = 0x2492492492492493;
          }
          else {
            uVar16 = 0x1e;
            plVar23 = (long *)&DAT_00188d50;
            do {
              uVar18 = uVar16 >> 1;
              uVar20 = ~uVar18 + uVar16;
              uVar16 = uVar18;
              if (*(uint *)(plVar23 + uVar18 * 2 + 1) < uVar25) {
                plVar23 = plVar23 + uVar18 * 2 + 2;
                uVar16 = uVar20;
              }
            } while (uVar16 != 0);
            lVar22 = *plVar23;
            uVar16 = plVar23[1];
          }
          if ((uVar16 & 0xffffffff) < uVar27) {
            param_1[0x20] = lVar22;
            *(int *)(param_1 + 0x21) = (int)uVar16;
            *(short *)((long)param_1 + 0x10c) = (short)(uVar16 >> 0x20);
          }
        }
      }
                    /* try { // try from 0065f992 to 0065f9a6 has its CatchHandler @ 00660220 */
      ppuVar15 = local_88;
      FUN_006083a2(local_d0,local_88);
      local_108 = (undefined **)&DAT_001f0700;
      uStack_100 = 0;
      if (ppuVar15 != (undefined **)0x0) {
        ppuVar15 = (undefined **)0x0;
        do {
                    /* try { // try from 0065f9d1 to 0065f9e5 has its CatchHandler @ 006600a8 */
          local_80 = ppuVar15;
          FUN_003b837e(&local_e8,&local_108);
          lVar22 = 8;
          puVar26 = &uStack_100;
          do {
            if (lVar22 == 0) break;
                    /* try { // try from 0065f9f9 to 0065fa04 has its CatchHandler @ 0066021e */
            lVar12 = (*(code *)local_e8[2])(&local_e8,puVar26,lVar22);
            puVar26 = (ulong *)((long)puVar26 + lVar12);
            lVar22 = lVar22 - lVar12;
          } while (lVar12 != 0);
          uVar16 = uStack_100;
          ppuVar15 = local_108;
          lVar22 = (long)puVar26 - (long)&uStack_100;
          if (lVar22 != 8) {
                    /* try { // try from 0065fcac to 0065fcc4 has its CatchHandler @ 00660225 */
                    /* WARNING: Subroutine does not return */
            FUN_003b84d2(8,lVar22,"pod type",8);
          }
          bVar28 = local_108 != (undefined **)0x0;
          bVar29 = local_108 != (undefined **)&DAT_001f0700;
          if (bVar29 && bVar28) {
            LOCK();
            *local_108 = *local_108 + 1;
            UNLOCK();
          }
                    /* try { // try from 0065fa55 to 0065fb5d has its CatchHandler @ 006600ad */
          FUN_006083a2(local_d0,param_1[0x22] + 1);
          lVar12 = FUN_0031f260(ppuVar15,param_1[0x20],param_1[0x21]);
          lVar22 = param_1[0x1f];
          puVar26 = *(ulong **)(lVar22 + lVar12 * 8);
          if (puVar26 == (ulong *)0x0) {
            puVar13 = (ulong *)FUN_00935ae9(0x18);
            *puVar13 = 1;
            puVar13[1] = (ulong)ppuVar15;
            if (bVar29 && bVar28) {
              LOCK();
              *ppuVar15 = *ppuVar15 + 1;
              UNLOCK();
              lVar22 = *local_d0;
            }
            puVar13[2] = uVar16;
            puVar26 = (ulong *)(lVar22 + lVar12 * 8 + 8U | 1);
          }
          else {
            local_68 = uVar16;
            local_78 = lVar22;
            local_70 = lVar12;
            if (((ulong)puVar26 & 1) == 0) {
              bVar1 = *(byte *)(ppuVar15 + 1);
              if ((bVar1 & 1) == 0) {
                puVar17 = (undefined *)(ulong)(bVar1 >> 1);
              }
              else {
                puVar17 = ppuVar15[2];
              }
              puVar13 = puVar26;
              do {
                uVar16 = puVar13[1];
                bVar2 = *(byte *)(uVar16 + 8);
                if ((bVar2 & 1) == 0) {
                  puVar14 = (undefined *)(ulong)(bVar2 >> 1);
                }
                else {
                  puVar14 = *(undefined **)(uVar16 + 0x10);
                }
                if (puVar14 == puVar17) {
                  puVar14 = (undefined *)((long)ppuVar15 + 9);
                  if ((bVar1 & 1) != 0) {
                    puVar14 = ppuVar15[3];
                  }
                  if ((bVar2 & 1) == 0) {
                    pvVar11 = (void *)(uVar16 + 9);
                  }
                  else {
                    pvVar11 = *(void **)(uVar16 + 0x18);
                  }
                  iVar8 = memcmp(pvVar11,puVar14,(size_t)puVar17);
                  if (iVar8 == 0) goto LAB_0065fbab;
                }
                puVar13 = (ulong *)*puVar13;
              } while (((ulong)puVar13 & 1) == 0);
            }
            puVar13 = (ulong *)FUN_00935ae9(0x18);
            *puVar13 = 1;
            puVar13[1] = (ulong)ppuVar15;
            lVar22 = local_78;
            if (bVar29 && bVar28) {
              LOCK();
              *ppuVar15 = *ppuVar15 + 1;
              UNLOCK();
              lVar22 = *local_d0;
            }
            puVar13[2] = local_68;
            lVar12 = local_70;
          }
          *puVar13 = (ulong)puVar26;
          *(ulong **)(lVar22 + lVar12 * 8) = puVar13;
          param_1[0x22] = param_1[0x22] + 1;
LAB_0065fbab:
          ppuVar6 = local_88;
          if (bVar29 && bVar28) {
            if (*ppuVar15 != (undefined *)0x1) {
              LOCK();
              *ppuVar15 = *ppuVar15 + -1;
              UNLOCK();
              if (*ppuVar15 != (undefined *)0x0) goto LAB_0065fbf6;
            }
            if (((ulong)ppuVar15[1] & 1) != 0) {
              free(ppuVar15[3]);
            }
            free(ppuVar15);
          }
LAB_0065fbf6:
          __ptr = local_108;
          ppuVar15 = (undefined **)((long)local_80 + 1);
        } while (ppuVar15 != ppuVar6);
        if ((local_108 != (undefined **)0x0) && (local_108 != (undefined **)&DAT_001f0700)) {
          if (*local_108 != (undefined *)0x1) {
            LOCK();
            *local_108 = *local_108 + -1;
            UNLOCK();
            if (*local_108 != (undefined *)0x0) goto LAB_0065fc43;
          }
          if (((ulong)local_108[1] & 1) != 0) {
            free(local_108[3]);
          }
          free(__ptr);
        }
      }
LAB_0065fc43:
      if (local_b8 != (long *)0x0) {
        (**(code **)(*local_b8 + 0x18))();
      }
      plVar23 = local_b0;
      if ((local_b0 != (long *)0x0) && (local_b0 != &DAT_001f0700)) {
        if (*local_b0 != 1) {
          LOCK();
          *local_b0 = *local_b0 + -1;
          UNLOCK();
          if (*local_b0 != 0) goto LAB_0065fc88;
        }
        if ((*(byte *)(local_b0 + 1) & 1) != 0) {
          free((void *)local_b0[3]);
        }
        free(plVar23);
      }
LAB_0065fc88:
      if (local_60 != (void *)0x0) {
        free(local_60);
      }
      return;
    }
  }
  uVar19 = 4;
LAB_0065fcca:
                    /* try { // try from 0065fcca to 0065fcdd has its CatchHandler @ 00660220 */
                    /* WARNING: Subroutine does not return */
  FUN_003b84d2(uVar19,lVar22,"pod type",8);
}



// ==================================================
// Decompiled: FUN_00660496 at 0x660496
// ==================================================


void FUN_00660496(long *param_1,long *param_2)

{
  long *plVar1;
  long lVar2;
  undefined4 uVar3;
  undefined4 uVar4;
  undefined4 uVar5;
  undefined4 uVar6;
  long *plVar7;
  void *__s;
  long lVar8;
  undefined8 local_e0;
  undefined8 uStack_d8;
  long *local_d0;
  undefined1 local_c8 [16];
  long *local_b8;
  undefined1 local_b0 [16];
  long *local_a0;
  undefined1 local_98 [16];
  long *local_88;
  undefined1 local_80 [16];
  long *local_70;
  undefined1 local_68 [16];
  long *local_58;
  undefined1 local_50 [16];
  long *local_40;
  undefined1 local_38 [16];
  long *local_28;
  
  (**(code **)(*param_2 + 0x10))(local_38);
                    /* try { // try from 006604bf to 006604ce has its CatchHandler @ 0066082f */
  FUN_00661806(local_c8,param_2 + 3,local_38);
                    /* try { // try from 006604d7 to 006604e3 has its CatchHandler @ 0066081a */
  (**(code **)(param_2[0xd] + 0x10))(local_b0,param_2 + 0xd,local_c8);
                    /* try { // try from 006604eb to 006604f9 has its CatchHandler @ 00660805 */
  FUN_00661806(local_98,param_2 + 0x10,local_b0);
                    /* try { // try from 00660504 to 00660510 has its CatchHandler @ 006607f0 */
  (**(code **)(param_2[0x1a] + 0x10))(local_80,param_2 + 0x1a,local_98);
                    /* try { // try from 00660518 to 00660529 has its CatchHandler @ 006607db */
  FUN_006615a4(local_68,param_2 + 0x1d,local_80);
                    /* try { // try from 00660534 to 00660546 has its CatchHandler @ 006607c3 */
  (**(code **)(param_2[0x22] + 0x10))(local_50,param_2 + 0x22,local_68);
                    /* try { // try from 0066054e to 0066055f has its CatchHandler @ 006607ab */
  FUN_006615a4(&local_e0,param_2 + 0x25,local_50);
                    /* try { // try from 0066056a to 00660574 has its CatchHandler @ 00660796 */
  (**(code **)(param_2[0x2a] + 0x10))(param_1,param_2 + 0x2a,&local_e0);
  if (local_d0 != (long *)0x0) {
    (**(code **)(*local_d0 + 0x18))();
  }
  if (local_40 != (long *)0x0) {
    (**(code **)(*local_40 + 0x18))();
  }
  if (local_58 != (long *)0x0) {
    (**(code **)(*local_58 + 0x18))();
  }
  if (local_70 != (long *)0x0) {
    (**(code **)(*local_70 + 0x18))();
  }
  if (local_88 != (long *)0x0) {
    (**(code **)(*local_88 + 0x18))();
  }
  if (local_a0 != (long *)0x0) {
    (**(code **)(*local_a0 + 0x18))();
  }
  if (local_b8 != (long *)0x0) {
    (**(code **)(*local_b8 + 0x18))();
  }
  if (local_28 != (long *)0x0) {
    (**(code **)(*local_28 + 0x18))();
  }
  lVar8 = *(long *)*param_1;
  param_2[0x2f] = (long)((long *)*param_1 + 1);
                    /* try { // try from 00660617 to 00660623 has its CatchHandler @ 0066078a */
  FUN_003a1310(&local_e0,param_1,lVar8 + 8,param_1[1]);
  *param_1 = (long)local_e0;
  param_1[1] = uStack_d8;
  plVar1 = (long *)param_1[2];
  param_1[2] = (long)local_d0;
  plVar7 = local_e0;
  lVar8 = uStack_d8;
  if (plVar1 != (long *)0x0) {
    (**(code **)(*plVar1 + 0x18))();
    plVar7 = (long *)*param_1;
    lVar8 = param_1[1];
  }
  lVar2 = *plVar7;
  param_2[0x30] = (long)(plVar7 + 1);
                    /* try { // try from 00660666 to 00660672 has its CatchHandler @ 00660788 */
  FUN_003a1310(&local_e0,param_1,lVar2 + 8,lVar8);
  *param_1 = (long)local_e0;
  param_1[1] = uStack_d8;
  plVar1 = (long *)param_1[2];
  param_1[2] = (long)local_d0;
  plVar7 = local_e0;
  lVar8 = uStack_d8;
  if (plVar1 != (long *)0x0) {
    (**(code **)(*plVar1 + 0x18))();
    plVar7 = (long *)*param_1;
    lVar8 = param_1[1];
  }
  lVar2 = *plVar7;
                    /* try { // try from 006606a6 to 006606b7 has its CatchHandler @ 00660786 */
  FUN_003a1310(&local_e0,param_1,8,lVar8);
  *(undefined4 *)param_1 = (undefined4)local_e0;
  *(undefined4 *)((long)param_1 + 4) = local_e0._4_4_;
  *(undefined4 *)(param_1 + 1) = (undefined4)uStack_d8;
  *(undefined4 *)((long)param_1 + 0xc) = uStack_d8._4_4_;
  plVar1 = (long *)param_1[2];
  param_1[2] = (long)local_d0;
  if (plVar1 != (long *)0x0) {
    (**(code **)(*plVar1 + 0x18))();
  }
  if (lVar2 != 0) {
                    /* try { // try from 006606e1 to 006606ea has its CatchHandler @ 00660784 */
    __s = (void *)FUN_00935ae9(400);
    memset(__s,0,400);
    FUN_00660364(__s);
    if ((void *)param_2[0x31] != __s) {
      if ((void *)param_2[0x31] != (void *)0x0) {
        FUN_0066042e();
      }
      param_2[0x31] = (long)__s;
    }
                    /* try { // try from 00660722 to 00660731 has its CatchHandler @ 00660782 */
    FUN_00660496(&local_e0,__s,param_1);
    plVar7 = local_d0;
    uVar3 = (undefined4)local_e0;
    uVar4 = local_e0._4_4_;
    uVar5 = (undefined4)uStack_d8;
    uVar6 = uStack_d8._4_4_;
    local_e0 = (long *)0x0;
    uStack_d8 = 0;
    local_d0 = (long *)0x0;
    *(undefined4 *)param_1 = uVar3;
    *(undefined4 *)((long)param_1 + 4) = uVar4;
    *(undefined4 *)(param_1 + 1) = uVar5;
    *(undefined4 *)((long)param_1 + 0xc) = uVar6;
    plVar1 = (long *)param_1[2];
    param_1[2] = (long)plVar7;
    if (plVar1 != (long *)0x0) {
      (**(code **)(*plVar1 + 0x18))();
      if (local_d0 != (long *)0x0) {
        (**(code **)(*local_d0 + 0x18))();
      }
    }
  }
  return;
}



// ==================================================
// Decompiled: FUN_00661806 at 0x661806
// ==================================================


void FUN_00661806(undefined8 param_1,ulong *param_2,long *param_3)

{
  ulong uVar1;
  undefined1 auStack_38 [16];
  long *local_28;
  
  uVar1 = *(ulong *)*param_3;
  *param_2 = uVar1;
  param_2[1] = *(ulong *)(*param_3 + 8);
  param_2[2] = *(ulong *)(*param_3 + 0x10);
  if (((char)param_2[9] != '\0') && ((void *)param_2[3] != (void *)0x0)) {
    free((void *)param_2[3]);
    uVar1 = *param_2;
  }
  param_2[3] = *param_3 + 0x18;
  *(undefined1 *)(param_2 + 9) = 0;
  FUN_003a1310(auStack_38,param_3,uVar1 * 4 + (ulong)((uVar1 & 1) == 0) * 4 + 0x1c,param_3[1]);
                    /* try { // try from 0066188c to 00661899 has its CatchHandler @ 006618b6 */
  FUN_006615a4(param_1,param_2 + 4,auStack_38);
  if (local_28 != (long *)0x0) {
    (**(code **)(*local_28 + 0x18))();
  }
  return;
}



// ==================================================
// Decompiled: FUN_006615a4 at 0x6615a4
// ==================================================


void FUN_006615a4(undefined8 param_1,undefined8 *param_2,long *param_3)

{
  long lVar1;
  
  *param_2 = *(undefined8 *)*param_3;
  lVar1 = *(long *)(*param_3 + 8);
  param_2[1] = lVar1;
  param_2[2] = *(undefined8 *)(*param_3 + 0x10);
  if ((*(char *)(param_2 + 4) != '\0') && ((void *)param_2[3] != (void *)0x0)) {
    free((void *)param_2[3]);
    lVar1 = param_2[1];
  }
  param_2[3] = *param_3 + 0x18;
  *(undefined1 *)(param_2 + 4) = 0;
  FUN_003a1310(param_1,param_3,lVar1 * 8 + 0x20,param_3[1]);
  return;
}



// ==================================================
// Decompiled: FUN_00660364 at 0x660364
// ==================================================


void FUN_00660364(undefined8 *param_1)

{
  *param_1 = &PTR_FUN_00a112b0;
  param_1[1] = 0;
  param_1[2] = 0;
  param_1[3] = 0;
  param_1[4] = 0;
  param_1[5] = 0;
  param_1[6] = 0;
  param_1[7] = 0;
  param_1[8] = 0;
  param_1[9] = 0;
  param_1[10] = 0;
  *(undefined1 *)(param_1 + 0xb) = 1;
  *(undefined1 *)(param_1 + 0xc) = 1;
  param_1[0xd] = &PTR_FUN_00a112b0;
  param_1[0xe] = 0;
  param_1[0xf] = 0;
  param_1[0x10] = 0;
  param_1[0x11] = 0;
  param_1[0x12] = 0;
  param_1[0x13] = 0;
  param_1[0x14] = 0;
  param_1[0x15] = 0;
  param_1[0x16] = 0;
  param_1[0x17] = 0;
  *(undefined1 *)(param_1 + 0x18) = 1;
  *(undefined1 *)(param_1 + 0x19) = 1;
  param_1[0x1a] = &PTR_FUN_00a112b0;
  param_1[0x1b] = 0;
  param_1[0x1c] = 0;
  param_1[0x1d] = 0;
  param_1[0x1e] = 0;
  param_1[0x1f] = 0;
  param_1[0x20] = 0;
  *(undefined1 *)(param_1 + 0x21) = 1;
  param_1[0x22] = &PTR_FUN_00a112b0;
  param_1[0x23] = 0;
  param_1[0x24] = 0;
  param_1[0x25] = 0;
  param_1[0x26] = 0;
  param_1[0x27] = 0;
  param_1[0x28] = 0;
  *(undefined1 *)(param_1 + 0x29) = 1;
  param_1[0x2b] = 0;
  param_1[0x2c] = 0;
  param_1[0x2a] = &PTR_FUN_00a11260;
  *(undefined1 *)(param_1 + 0x2d) = 0;
  param_1[0x2e] = 0;
  param_1[0x31] = 0;
  return;
}



// ==================================================
// Decompiled: FUN_0066084e at 0x66084e
// ==================================================


undefined8 FUN_0066084e(undefined8 param_1,long param_2,long *param_3)

{
  ulong *puVar1;
  ulong uVar2;
  long lVar3;
  undefined8 *puVar4;
  undefined **local_48;
  undefined8 uStack_40;
  char *local_38;
  undefined8 local_30;
  undefined4 local_28;
  undefined4 uStack_24;
  
  puVar1 = (ulong *)*param_3;
  uVar2 = *puVar1;
  if (uVar2 < 0x100) {
    *(char *)(param_2 + 0x18) = (char)uVar2;
    *(ulong *)(param_2 + 0x20) = puVar1[1];
    FUN_003a1310(&local_38,param_3,0x10,param_3[1]);
    *(undefined8 *)(param_2 + 8) = *(undefined8 *)local_38;
    lVar3 = *(long *)(local_38 + 8);
    *(char **)(param_2 + 0x10) = local_38 + 0x10;
                    /* try { // try from 006608b6 to 006608c0 has its CatchHandler @ 00660966 */
    FUN_003a1310(param_1,&local_38,lVar3 * 8 + 0x10,local_30);
    if ((long *)CONCAT44(uStack_24,local_28) != (long *)0x0) {
      (**(code **)(*(long *)CONCAT44(uStack_24,local_28) + 0x18))();
    }
    return param_1;
  }
  puVar4 = (undefined8 *)FUN_003689a8(0x10);
  local_38 = "library/cpp/succinct_arrays/intvector.h";
  local_30 = 0x27;
  local_28 = 0x96;
  local_48 = (undefined **)0x0;
  uStack_40 = 0;
                    /* try { // try from 00660912 to 00660916 has its CatchHandler @ 006609b2 */
  FUN_0039e2b0();
  local_48 = &PTR_FUN_00a3e1d0;
                    /* try { // try from 00660927 to 00660965 has its CatchHandler @ 0066097b */
  FUN_00315621(&local_38);
  FUN_00660b5c(&local_48);
  puVar4[1] = uStack_40;
  uStack_40 = 0;
  *puVar4 = &PTR_FUN_00a3e1d0;
                    /* WARNING: Subroutine does not return */
  FUN_00368ab9(puVar4,&PTR_PTR_00a3e130,FUN_00315706);
}



// ==================================================
// Decompiled: FUN_00663c34 at 0x663c34
// ==================================================


void FUN_00663c34(long param_1)

{
  undefined8 uVar1;
  undefined8 *puVar2;
  ulong uVar3;
  ulong uVar4;
  ulong uVar5;
  ulong uVar6;
  
  uVar1 = FUN_0039f413();
  *(undefined8 *)(param_1 + 0x118) = uVar1;
  *(undefined8 *)(param_1 + 0x120) = 0;
  FUN_00665688(param_1 + 0x60);
  FUN_006656b2(param_1 + 0xb8);
  *(undefined8 *)(param_1 + 0x128) = 0;
  if (*(long *)(param_1 + 0x110) != 0) {
    FUN_0067cf20();
  }
  *(byte *)(param_1 + 0x58) = *(byte *)(param_1 + 0x58) & 0xfe;
  if (*(long *)(param_1 + 0x160) != 0) {
    uVar3 = (ulong)*(uint *)(param_1 + 0x158) + *(long *)(param_1 + 0x160) * 2;
    uVar6 = uVar3 / 3;
    FUN_00664508(param_1 + 0x148);
    if (uVar6 < *(uint *)(param_1 + 0x158)) {
      if (uVar3 < 0x18) {
        uVar3 = 0x200000007;
        uVar1 = 0x2492492492492493;
      }
      else {
        uVar3 = 0x1e;
        puVar2 = (undefined8 *)&DAT_00188d50;
        do {
          uVar4 = uVar3 >> 1;
          uVar5 = ~uVar4 + uVar3;
          uVar3 = uVar4;
          if (*(uint *)(puVar2 + uVar4 * 2 + 1) < uVar6) {
            puVar2 = puVar2 + uVar4 * 2 + 2;
            uVar3 = uVar5;
          }
        } while (uVar3 != 0);
        uVar1 = *puVar2;
        uVar3 = puVar2[1];
      }
      if ((uVar3 & 0xffffffff) < (ulong)*(uint *)(param_1 + 0x158)) {
        *(undefined8 *)(param_1 + 0x150) = uVar1;
        *(int *)(param_1 + 0x158) = (int)uVar3;
        *(short *)(param_1 + 0x15c) = (short)(uVar3 >> 0x20);
      }
    }
  }
  if (*(long *)(param_1 + 0x180) != 0) {
    uVar3 = (ulong)*(uint *)(param_1 + 0x178) + *(long *)(param_1 + 0x180) * 2;
    uVar6 = uVar3 / 3;
    FUN_0033283e(param_1 + 0x168);
    if (uVar6 < *(uint *)(param_1 + 0x178)) {
      if (uVar3 < 0x18) {
        uVar3 = 0x200000007;
        uVar1 = 0x2492492492492493;
      }
      else {
        uVar3 = 0x1e;
        puVar2 = (undefined8 *)&DAT_00188d50;
        do {
          uVar4 = uVar3 >> 1;
          uVar5 = ~uVar4 + uVar3;
          uVar3 = uVar4;
          if (*(uint *)(puVar2 + uVar4 * 2 + 1) < uVar6) {
            puVar2 = puVar2 + uVar4 * 2 + 2;
            uVar3 = uVar5;
          }
        } while (uVar3 != 0);
        uVar1 = *puVar2;
        uVar3 = puVar2[1];
      }
      if ((uVar3 & 0xffffffff) < (ulong)*(uint *)(param_1 + 0x178)) {
        *(undefined8 *)(param_1 + 0x170) = uVar1;
        *(int *)(param_1 + 0x178) = (int)uVar3;
        *(short *)(param_1 + 0x17c) = (short)(uVar3 >> 0x20);
      }
    }
  }
  *(undefined8 *)(param_1 + 0x188) = 0;
  return;
}



// ==================================================
// Decompiled: FUN_00661b66 at 0x661b66
// ==================================================


undefined8 FUN_00661b66(undefined4 *param_1,undefined4 *param_2,undefined8 param_3)

{
  long lVar1;
  undefined4 uVar2;
  undefined4 uVar3;
  undefined4 uVar4;
  undefined4 uVar5;
  undefined4 uVar6;
  undefined4 uVar7;
  undefined4 uVar8;
  undefined8 in_RAX;
  
  lVar1 = *(long *)(param_2 + 8);
  *(long *)(param_1 + 8) = lVar1;
  uVar2 = *param_2;
  uVar3 = param_2[1];
  uVar4 = param_2[2];
  uVar5 = param_2[3];
  uVar6 = param_2[5];
  uVar7 = param_2[6];
  uVar8 = param_2[7];
  param_1[4] = param_2[4];
  param_1[5] = uVar6;
  param_1[6] = uVar7;
  param_1[7] = uVar8;
  *param_1 = uVar2;
  param_1[1] = uVar3;
  param_1[2] = uVar4;
  param_1[3] = uVar5;
  if (lVar1 == 0) {
                    /* WARNING: Subroutine does not return */
    FUN_0039ed52(&PTR_s_keyboard_suggest_succinct_trie_l_00a11398);
  }
  if (lVar1 - 1U < 3) {
    *(undefined8 *)(param_1 + lVar1 * 2) = param_3;
    return in_RAX;
  }
                    /* WARNING: Subroutine does not return */
  FUN_00319a27("array::at");
}



// ==================================================
// Decompiled: FUN_00661926 at 0x661926
// ==================================================


ulong FUN_00661926(undefined4 *param_1,undefined1 *param_2,undefined8 *param_3,long *param_4)

{
  ulong uVar1;
  uint uVar2;
  long lVar3;
  undefined8 uVar4;
  undefined8 uVar5;
  long lVar6;
  undefined7 uVar8;
  ulong uVar7;
  undefined4 local_58;
  undefined4 uStack_54;
  undefined4 uStack_50;
  undefined4 uStack_4c;
  undefined4 local_48;
  undefined4 uStack_44;
  undefined4 uStack_40;
  undefined4 uStack_3c;
  long local_38;
  
  lVar3 = *param_4;
  FUN_00660c82(lVar3);
  uVar7 = *(ulong *)(param_1 + 8);
  if (1 < uVar7) {
    lVar6 = uVar7 - 1;
    do {
      lVar3 = *(long *)(lVar3 + 0x188);
      lVar6 = lVar6 + -1;
    } while (lVar6 != 0);
  }
  if (uVar7 == 0) {
                    /* WARNING: Subroutine does not return */
    FUN_0039ed52(&PTR_s_keyboard_suggest_succinct_trie_l_00a11370);
  }
  if (2 < uVar7 - 1) {
                    /* WARNING: Subroutine does not return */
    FUN_00319a27("array::at");
  }
  uVar1 = *(ulong *)(param_1 + uVar7 * 2);
  if (uVar1 == 0) {
    if (uVar7 == 1) {
      uVar7 = 0;
      goto LAB_00661ad9;
    }
    local_38 = *(long *)(param_1 + 8);
    local_58 = *param_1;
    uStack_54 = param_1[1];
    uStack_50 = param_1[2];
    uStack_4c = param_1[3];
    local_48 = param_1[4];
    uStack_44 = param_1[5];
    uStack_40 = param_1[6];
    uStack_3c = param_1[7];
    if (local_38 == 0) {
                    /* WARNING: Subroutine does not return */
      FUN_0039ed52(&PTR_s_keyboard_suggest_succinct_trie_l_00a11348);
    }
    local_38 = local_38 + -1;
  }
  else {
    lVar6 = *(long *)(lVar3 + 0x188);
    uVar8 = (undefined7)((ulong)param_3 >> 8);
    if (lVar6 == 0) {
      uVar7 = CONCAT71(uVar8,1);
      if ((param_2 != (undefined1 *)0x0) && (param_3 != (undefined8 *)0x0)) {
        lVar6 = 0;
        if (*(char *)(*(long *)(lVar3 + 0x180) + 1 + uVar1) != '\0') {
          lVar6 = uVar1 + 1;
        }
        *param_2 = *(undefined1 *)(*(long *)(lVar3 + 0x180) + uVar1);
        FUN_00661b66(&local_58,param_1,lVar6);
        param_3[4] = local_38;
        param_3[2] = CONCAT44(uStack_44,local_48);
        param_3[3] = CONCAT44(uStack_3c,uStack_40);
        *param_3 = CONCAT44(uStack_54,local_58);
        param_3[1] = CONCAT44(uStack_4c,uStack_50);
      }
      goto LAB_00661ad9;
    }
    lVar3 = FUN_00661538(lVar6 + 0xa0,*(undefined8 *)(lVar6 + 0x78),uVar1);
    uVar4 = FUN_00661646(lVar6 + 0x18,*(undefined8 *)(lVar6 + 0x10),lVar3 + -1);
    if ((*(ulong *)(*(long *)(lVar6 + 0x120) + (uVar1 >> 6) * 8) >> (uVar1 & 0x3f) & 1) == 0) {
      uVar7 = CONCAT71(uVar8,1);
      if ((param_2 != (undefined1 *)0x0) && (param_3 != (undefined8 *)0x0)) {
        *param_2 = *(undefined1 *)(*(long *)(lVar6 + 0x178) + uVar1);
        FUN_00661b66(&local_58,param_1,uVar4);
        param_3[4] = local_38;
        param_3[2] = CONCAT44(uStack_44,local_48);
        param_3[3] = CONCAT44(uStack_3c,uStack_40);
        *param_3 = CONCAT44(uStack_54,local_58);
        param_3[1] = CONCAT44(uStack_4c,uStack_50);
      }
      goto LAB_00661ad9;
    }
    uVar5 = FUN_00661538(lVar6 + 0x128,*(long *)(lVar6 + 0x120),uVar1);
    uVar5 = FUN_00661462(lVar6,uVar1,uVar5);
    FUN_00661baa(&local_58,param_1,uVar4,uVar5);
  }
  uVar2 = FUN_00661926(&local_58,param_2,param_3,param_4);
  uVar7 = (ulong)uVar2;
LAB_00661ad9:
  return uVar7 & 0xffffffff;
}



// ==================================================
// Decompiled: FUN_00661538 at 0x661538
// ==================================================


long FUN_00661538(long *param_1,long param_2,ulong param_3)

{
  if (param_3 < (ulong)(*param_1 << 6)) {
    return POPCOUNT(~(-1L << ((byte)param_3 & 0x3f)) & *(ulong *)(param_2 + (param_3 >> 6) * 8)) +
           (ulong)((uint)(*(ulong *)(param_1[3] + (param_3 >> 5 & 0xfffffffffffffff8 | 8)) >>
                         (((char)(param_3 >> 6) - 1U & 7) * '\t' & 0x3f)) & 0x1ff) +
           *(long *)(param_1[3] + (param_3 >> 5 & 0xfffffffffffffff0));
  }
  return param_1[2];
}



// ==================================================
// Decompiled: FUN_00661646 at 0x661646
// ==================================================


long FUN_00661646(long param_1,long param_2,ulong param_3)

{
  long lVar1;
  long lVar2;
  ulong uVar3;
  ulong uVar4;
  ulong uVar5;
  ulong uVar6;
  byte bVar7;
  uint uVar8;
  int iVar9;
  long lVar10;
  ulong uVar11;
  
  lVar2 = -1;
  if (param_3 < *(ulong *)(param_1 + 0x30)) {
    uVar3 = param_3 >> (*(byte *)(param_1 + 0x10) & 0x3f);
    uVar6 = (ulong)*(uint *)(*(long *)(param_1 + 0x18) + 4 + uVar3 * 4);
    lVar2 = *(long *)(param_1 + 0x38);
    if (param_3 < *(ulong *)(lVar2 + uVar6 * 8)) {
      uVar11 = (ulong)*(uint *)(*(long *)(param_1 + 0x18) + uVar3 * 4);
      uVar4 = uVar6 - uVar11;
      uVar3 = uVar6;
      while (uVar6 = uVar11, 2 < uVar4) {
        uVar5 = uVar6 + uVar3 >> 1 & 0xfffffffffffffffe;
        if (*(ulong *)(lVar2 + uVar5 * 8) <= param_3) {
          uVar6 = uVar5;
          uVar5 = uVar3;
        }
        uVar11 = uVar6;
        uVar3 = uVar5;
        uVar4 = uVar5 - uVar6;
      }
    }
    lVar10 = param_3 - *(long *)(lVar2 + uVar6 * 8);
    uVar4 = lVar10 * 0x40201008040201;
    uVar3 = *(ulong *)(lVar2 + 8 + uVar6 * 8);
    uVar4 = ((~uVar4 & uVar3 ^
             (uVar4 ^ uVar3 | (uVar4 | 0x4020100804020100) - (uVar3 & 0x3fdfeff7fbfdfeff))) >> 8 &
            0x40201008040201) * 0x40201008040201;
    lVar1 = (ulong)((uint)(uVar4 >> 0x36) & 7) + uVar6 * 4;
    iVar9 = (int)lVar10 -
            ((uint)(uVar3 >> (((char)(uVar4 >> 0x36) - 1U & 7) * '\t' & 0x3f)) & 0x1ff);
    uVar6 = *(ulong *)(param_2 + lVar1 * 8);
    lVar2 = 0x40;
    if (iVar9 < (int)POPCOUNT(uVar6)) {
      uVar3 = uVar6 - (uVar6 >> 1 & 0x5555555555555555);
      uVar3 = (uVar3 >> 2 & 0x3333333333333333) + (uVar3 & 0x3333333333333333);
      uVar3 = (uVar3 >> 4) + uVar3 & 0xf0f0f0f0f0f0f0f;
      uVar8 = (int)POPCOUNT(((long)iVar9 * 0x101010101010101 | 0x8080808080808080U) +
                            uVar3 * -0x101010101010101 & 0x8080808080808080) << 3;
      bVar7 = (byte)uVar8;
      lVar2 = (ulong)(byte)(&DAT_001c77b0)
                           [((long)iVar9 - (uVar3 * 0x101010101010100 >> (bVar7 & 0x3f) & 0xff)) *
                            0x100 | uVar6 >> (bVar7 & 0x3f) & 0xff] + (ulong)uVar8;
    }
    lVar2 = lVar1 * 0x40 + lVar2;
  }
  return lVar2;
}



