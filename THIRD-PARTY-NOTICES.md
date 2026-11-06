# 第三方许可声明（THIRD-PARTY NOTICES）

本文件是 **`harness_gui.exe` 及其任何嵌入了 ECDI 的二进制产物**的分发文档。
构建时由 `gui/CMakeLists.txt` 的 POST_BUILD 步骤复制到可执行文件同目录，`install()` 也装同一份。
二进制分发时**请保持本文件与 exe 同目录、原样不改**。

> 维护约定见 §5：换 ECDI 库或升级 FreeType 时，§3 的指纹与 §2 的版本必须同步更新，
> 否则本文件就会变成一份"对不上号"的声明。

## 1. 本产品声明（FreeType / FTL）

This product is based in part on the work of the FreeType Team (https://freetype.org).

Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org).
All rights reserved.

> 第二段是 FreeType Team 在 FTL 引言中给出的首选致谢格式（`encouraged`，非强制），
> `<year>` 取所用版本的版权年；本项目所用 2.14.3 为 2026 年发布，故取 2026。

## 2. 第三方组件清单

| 组件 | 版本 | 许可 | 在本产物中的形态 |
|---|---|---|---|
| ECDI | 见 §3 指纹 | MIT（Copyright © 2026 momoya-zh） | 静态库 `third_party/ecdi/lib/ECDI.lib`；经 `target_link_libraries` 静态链入 `harness_gui.exe` |
| FreeType | 2.14.3 | 双许可 FTL / GPLv2；**本项目取 FTL** | 由 ECDI 以 OBJECT 库方式并入 `ECDI.lib`（符号 `FT_Init_FreeType` / `FT_Done_FreeType` / `FreeTypeTextMeasurer`），随 exe 一起分发 |

FreeType 源码中另有若干自带许可的文件（均与 FTL 兼容，说明见附录 A 的相应段落）：

| 路径 | 许可 |
|---|---|
| `src/bdf/`、`src/pcf/`、`src/base/fthash.c`、`include/freetype/internal/fthash.h` | X11 风格 |
| `src/gzip/`（`zlib.h`） | zlib |
| `src/autofit/ft-hb-ft.c`、`ft-hb-decls.h`、`ft-hb-types.h`、`hb-script-list.h` | Old MIT（来自 HarfBuzz） |

这些文件只以**目标码**形态进入本产物。若再分发 FreeType **源码**，须原样保留上述文件及其许可头。

## 3. 二进制指纹（可复核）

| 项 | 值 |
|---|---|
| 文件 | `third_party/ecdi/lib/ECDI.lib`（按 `.gitignore` 不入库，随交付/安装产生） |
| 大小 | 11,975,450 字节 |
| SHA256 | `c520834698f34e6305ed8c7b89174e06f5ebedf3cd1fee22b15702f236e13da4` |
| 测量时间 | 2026-11-06 |
| 复核命令 | `Get-FileHash third_party\ecdi\lib\ECDI.lib -Algorithm SHA256` |
| FreeType 来源 | ECDI 仓 `third_party/freetype/`（2.14.3，vendored，零本地改动；见其 `README.ecdi.md`） |

★ 本表以**实际链入的那个库**为准。ECDI 侧交付单上的指纹若与此不同，以本表 + 复核命令的结果为准，
并按 §5 先对账再更新。

## 4. 再分发义务

| 形态 | 义务 |
|---|---|
| **二进制**（本产物的常态） | 随附本文件即满足 FTL §2 第二项。**无需**提供 FreeType 源码，也**不要求**本产品开源——FTL 非 copyleft |
| **源码**（若分发含 FreeType 源码的包） | 原样保留 `FTL.TXT`；任何改动须在文档中标明；保留版权声明（本文件附录 B 即逐字全文） |
| **命名** | FTL §3：可以致谢，但未经许可不得用 FreeType 名称做商业、广告或推广背书 |

## 5. 维护约定

1. **替换 `ECDI.lib`**：重测 §3 指纹，确认 FreeType 版本未变，再改表；同时确认前缀的
   `share/licenses/ECDI/` 五件套随新安装落盘（ECDI 自 2026-11-06 起 `install()` 自动装，
   见其 `docs/usage.md` §14.1；本前缀 2026-11-06 为手工补录）；
2. **ECDI 升级 FreeType**：更新 §2 版本、§1 的年份、附录全文（整段替换，不要手改）；
3. **附录必须逐字**：附录 A / B 是许可原文，只可整段替换，不可改写、不可删行；
4. 本文件是**入库资产**（不在 `third_party/` 忽略范围内），改动走正常提交。

---

## 附录 A — FreeType `LICENSE.TXT`（许可入口，逐字转载）

以下内容逐字转载自 FreeType 2.14.3 的 `LICENSE.TXT`，仅置于代码块内以便阅读：

```text
FREETYPE LICENSES
-----------------

The FreeType  2 font  engine is  copyrighted work  and cannot  be used
legally without  a software  license.  In order  to make  this project
usable to  a vast majority of  developers, we distribute it  under two
mutually exclusive open-source licenses.

This means that *you* must choose  *one* of the two licenses described
below, then obey all its terms and conditions when using FreeType 2 in
any of your projects or products.

  - The FreeType License,  found in the file  `docs/FTL.TXT`, which is
    similar to the  original BSD license *with*  an advertising clause
    that forces  you to explicitly  cite the FreeType project  in your
    product's  documentation.  All  details are  in the  license file.
    This license is suited to products which don't use the GNU General
    Public License.

    Note that  this license  is compatible to  the GNU  General Public
    License version 3, but not version 2.

  - The   GNU   General   Public   License   version   2,   found   in
    `docs/GPLv2.TXT`  (any  later  version  can  be  used  also),  for
    programs  which  already  use  the  GPL.  Note  that  the  FTL  is
    incompatible with GPLv2 due to its advertisement clause.

The contributed  BDF and PCF  drivers come  with a license  similar to
that  of the  X Window  System.   It is  compatible to  the above  two
licenses (see files `src/bdf/README`  and `src/pcf/README`).  The same
holds   for   the   source    code   files   `src/base/fthash.c`   and
`include/freetype/internal/fthash.h`; they were part of the BDF driver
in earlier FreeType versions.

The gzip  module uses the  zlib license (see  `src/gzip/zlib.h`) which
too is compatible to the above two licenses.

The   files   `src/autofit/ft-hb-ft.c`,   `src/autofit/ft-hb-decls.h`,
`src/autofit/ft-hb-types.h`,     and    `src/autofit/hb-script-list.h`
contain code taken (almost) verbatim  from the HarfBuzz library, which
uses the 'Old MIT' license compatible to the above two licenses.

The  MD5 checksum  support  (only used  for  debugging in  development
builds) is in the public domain.


--- end of LICENSE.TXT ---
```

## 附录 B — FreeType `docs/FTL.TXT`（FTL 全文，逐字转载）

以下内容逐字转载自 FreeType 2.14.3 的 `docs/FTL.TXT`（本项目取用的许可），仅置于代码块内以便阅读：

```text
                    The FreeType Project LICENSE
                    ----------------------------

                            2006-Jan-27

                    Copyright 1996-2002, 2006 by
          David Turner, Robert Wilhelm, and Werner Lemberg



Introduction
============

  The FreeType  Project is distributed in  several archive packages;
  some of them may contain, in addition to the FreeType font engine,
  various tools and  contributions which rely on, or  relate to, the
  FreeType Project.

  This  license applies  to all  files found  in such  packages, and
  which do not  fall under their own explicit  license.  The license
  affects  thus  the  FreeType   font  engine,  the  test  programs,
  documentation and makefiles, at the very least.

  This  license   was  inspired  by  the  BSD,   Artistic,  and  IJG
  (Independent JPEG  Group) licenses, which  all encourage inclusion
  and  use of  free  software in  commercial  and freeware  products
  alike.  As a consequence, its main points are that:

    o We don't promise that this software works. However, we will be
      interested in any kind of bug reports. (`as is' distribution)

    o You can  use this software for whatever you  want, in parts or
      full form, without having to pay us. (`royalty-free' usage)

    o You may not pretend that  you wrote this software.  If you use
      it, or  only parts of it,  in a program,  you must acknowledge
      somewhere  in  your  documentation  that  you  have  used  the
      FreeType code. (`credits')

  We  specifically  permit  and  encourage  the  inclusion  of  this
  software, with  or without modifications,  in commercial products.
  We  disclaim  all warranties  covering  The  FreeType Project  and
  assume no liability related to The FreeType Project.


  Finally,  many  people  asked  us  for  a  preferred  form  for  a
  credit/disclaimer to use in compliance with this license.  We thus
  encourage you to use the following text:

   """
    Portions of this software are copyright © <year> The FreeType
    Project (https://freetype.org).  All rights reserved.
   """

  Please replace <year> with the value from the FreeType version you
  actually use.


Legal Terms
===========

0. Definitions
--------------

  Throughout this license,  the terms `package', `FreeType Project',
  and  `FreeType  archive' refer  to  the  set  of files  originally
  distributed  by the  authors  (David Turner,  Robert Wilhelm,  and
  Werner Lemberg) as the `FreeType Project', be they named as alpha,
  beta or final release.

  `You' refers to  the licensee, or person using  the project, where
  `using' is a generic term including compiling the project's source
  code as  well as linking it  to form a  `program' or `executable'.
  This  program is  referred to  as  `a program  using the  FreeType
  engine'.

  This  license applies  to all  files distributed  in  the original
  FreeType  Project,   including  all  source   code,  binaries  and
  documentation,  unless  otherwise  stated   in  the  file  in  its
  original, unmodified form as  distributed in the original archive.
  If you are  unsure whether or not a particular  file is covered by
  this license, you must contact us to verify this.

  The FreeType  Project is copyright (C) 1996-2000  by David Turner,
  Robert Wilhelm, and Werner Lemberg.  All rights reserved except as
  specified below.

1. No Warranty
--------------

  THE FREETYPE PROJECT  IS PROVIDED `AS IS' WITHOUT  WARRANTY OF ANY
  KIND, EITHER  EXPRESS OR IMPLIED,  INCLUDING, BUT NOT  LIMITED TO,
  WARRANTIES  OF  MERCHANTABILITY   AND  FITNESS  FOR  A  PARTICULAR
  PURPOSE.  IN NO EVENT WILL ANY OF THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE  FOR ANY DAMAGES CAUSED  BY THE USE OR  THE INABILITY TO
  USE, OF THE FREETYPE PROJECT.

2. Redistribution
-----------------

  This  license  grants  a  worldwide, royalty-free,  perpetual  and
  irrevocable right  and license to use,  execute, perform, compile,
  display,  copy,   create  derivative  works   of,  distribute  and
  sublicense the  FreeType Project (in  both source and  object code
  forms)  and  derivative works  thereof  for  any  purpose; and  to
  authorize others  to exercise  some or all  of the  rights granted
  herein, subject to the following conditions:

    o Redistribution of  source code  must retain this  license file
      (`FTL.TXT') unaltered; any  additions, deletions or changes to
      the original  files must be clearly  indicated in accompanying
      documentation.   The  copyright   notices  of  the  unaltered,
      original  files must  be  preserved in  all  copies of  source
      files.

    o Redistribution in binary form must provide a  disclaimer  that
      states  that  the software is based in part of the work of the
      FreeType Team,  in  the  distribution  documentation.  We also
      encourage you to put an URL to the FreeType web page  in  your
      documentation, though this isn't mandatory.

  These conditions  apply to any  software derived from or  based on
  the FreeType Project,  not just the unmodified files.   If you use
  our work, you  must acknowledge us.  However, no  fee need be paid
  to us.

3. Advertising
--------------

  Neither the  FreeType authors and  contributors nor you  shall use
  the name of the  other for commercial, advertising, or promotional
  purposes without specific prior written permission.

  We suggest,  but do not require, that  you use one or  more of the
  following phrases to refer  to this software in your documentation
  or advertising  materials: `FreeType Project',  `FreeType Engine',
  `FreeType library', or `FreeType Distribution'.

  As  you have  not signed  this license,  you are  not  required to
  accept  it.   However,  as  the FreeType  Project  is  copyrighted
  material, only  this license, or  another one contracted  with the
  authors, grants you  the right to use, distribute,  and modify it.
  Therefore,  by  using,  distributing,  or modifying  the  FreeType
  Project, you indicate that you understand and accept all the terms
  of this license.

4. Contacts
-----------

  There are two mailing lists related to FreeType:

    o freetype@nongnu.org

      Discusses general use and applications of FreeType, as well as
      future and  wanted additions to the  library and distribution.
      If  you are looking  for support,  start in  this list  if you
      haven't found anything to help you in the documentation.

    o freetype-devel@nongnu.org

      Discusses bugs,  as well  as engine internals,  design issues,
      specific licenses, porting, etc.

  Our home page can be found at

    https://freetype.org


--- end of FTL.TXT ---
```
