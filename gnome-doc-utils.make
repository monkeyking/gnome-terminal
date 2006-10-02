# gnome-doc-utils.make - make magic for building documentation
# Copyright (C) 2004-2005 Shaun McCance <shaunm@gnome.org>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
# As a special exception to the GNU General Public License, if you
# distribute this file as part of a program that contains a
# configuration script generated by Autoconf, you may include it under
# the same distribution terms that you use for the rest of that program.

################################################################################
## @@ Generating Header Files

## @ DOC_H_FILE
## The name of the header file to generate
DOC_H_FILE ?=

## @ DOC_H_DOCS
## The input DocBook files for generating the header file
DOC_H_DOCS ?=

$(DOC_H_FILE): $(DOC_H_DOCS);
	@rm -f $@.tmp; touch $@.tmp;
	echo 'const gchar* documentation_credits[] = {' >> $@.tmp
	list='$(DOC_H_DOCS)'; for doc in $$list; do \
	  xmlpath="`echo $$doc | sed -e 's/^\(.*\/\).*/\1/' -e '/\//!s/.*//'`:$(srcdir)/`echo $$doc | sed -e 's/^\(.*\/\).*/\1/' -e '/\//!s/.*//'`"; \
	  if ! test -f "$$doc"; then doc="$(srcdir)/$$doc"; fi; \
	  xsltproc --path "$$xmlpath" $(_credits) $$doc; \
	done | sort | uniq \
	  | awk 'BEGIN{s=""}{n=split($$0,w,"<");if(s!=""&&s!=substr(w[1],1,length(w[1])-1)){print s};if(n>1){print $$0;s=""}else{s=$$0}};END{if(s!=""){print s}}' \
	  | sed -e 's/\\/\\\\/' -e 's/"/\\"/' -e 's/\(.*\)/\t"\1",/' >> $@.tmp
	echo '	NULL' >> $@.tmp
	echo '};' >> $@.tmp
	echo >> $@.tmp
	list='$(DOC_H_DOCS)'; for doc in $$list; do \
	  xmlpath="`echo $$doc | sed -e 's/^\(.*\/\).*/\1/' -e '/\//!s/.*//'`:$(srcdir)/`echo $$doc | sed -e 's/^\(.*\/\).*/\1/' -e '/\//!s/.*//'`"; \
	  if ! test -f "$$doc"; then doc="$(srcdir)/$$doc"; fi; \
	  docid=`echo "$$doc" | sed -e 's/.*\/\([^/]*\)\.xml/\1/' \
	    | sed -e 's/[^a-zA-Z_]/_/g' | tr 'a-z' 'A-Z'`; \
	  echo $$xmlpath; \
	  ids=`xsltproc --xinclude --path "$$xmlpath" $(_ids) $$doc`; \
	  for id in $$ids; do \
	    echo '#define HELP_'`echo $$docid`'_'`echo $$id \
	      | sed -e 's/[^a-zA-Z_]/_/g' | tr 'a-z' 'A-Z'`' "'$$id'"' >> $@.tmp; \
	  done; \
	  echo >> $@.tmp; \
	done;
	cp $@.tmp $@ && rm -f $@.tmp

.PHONY: dist-doc-header
dist-doc-header: $(DOC_H_FILE)
	@if test -f "$(DOC_H_FILE)"; then d=; else d="$(srcdir)/"; fi; \
	echo "$(INSTALL_DATA) $${d}$(DOC_H_FILE) $(distdir)/$(DOC_H_FILE)"; \
	$(INSTALL_DATA) "$${d}$(DOC_H_FILE)" "$(distdir)/$(DOC_H_FILE)";

doc-dist-hook: $(if $(DOC_H_FILE),dist-doc-header)

.PHONY: clean-doc-header
_clean_doc_header = $(if $(DOC_H_FILE),clean-doc-header)
clean-local: $(_clean_doc_header)
distclean-local: $(_clean_doc_header)
mostlyclean-local: $(_clean_doc_header)
maintainer-clean-local: $(_clean_doc_header)
clean-doc-header:
	rm -f $(DOC_H_FILE)

all: $(DOC_H_FILE)


################################################################################
## @@ Generating Documentation Files

## @ DOC_MODULE
## The name of the document being built
DOC_MODULE ?=

## @ DOC_ENTITIES
## Files included with a SYSTEM entity
DOC_ENTITIES ?=

## @ DOC_INCLUDES
## Files included with XInclude
DOC_INCLUDES ?=

## @ DOC_FIGURES
## Figures and other external data
DOC_FIGURES ?=

## @ DOC_FORMATS
## The default formats to be built and installed
DOC_FORMATS ?= docbook
_DOC_REAL_FORMATS = $(if $(DOC_USER_FORMATS),$(DOC_USER_FORMATS),$(DOC_FORMATS))

## @ DOC_LINGUAS
## The languages this document is translated into
DOC_LINGUAS ?=
_DOC_REAL_LINGUAS = $(if $(filter environment,$(origin LINGUAS)),		\
	$(filter $(LINGUAS),$(DOC_LINGUAS)),					\
	$(DOC_LINGUAS))

## @ RNGDOC_DIRS
## The directories containing RNG files to be documented with rngdoc
RNGDOC_DIRS ?=

## @ XSLDOC_DIRS
## The directories containing XSLT files to be documented with xsldoc
XSLDOC_DIRS ?=


################################################################################
## Variables for Bootstrapping

_xml2po ?= `which xml2po`

_db2html ?= `$(PKG_CONFIG) --variable db2html gnome-doc-utils`
_db2omf  ?= `$(PKG_CONFIG) --variable db2omf gnome-doc-utils`
_rngdoc  ?= `$(PKG_CONFIG) --variable rngdoc gnome-doc-utils`
_xsldoc  ?= `$(PKG_CONFIG) --variable xsldoc gnome-doc-utils`
_chunks  ?= `$(PKG_CONFIG) --variable xmldir gnome-doc-utils`/gnome/xslt/docbook/utils/chunks.xsl
_credits ?= `$(PKG_CONFIG) --variable xmldir gnome-doc-utils`/gnome/xslt/docbook/utils/credits.xsl
_ids ?= `$(PKG_CONFIG) --variable xmldir gnome-doc-utils`/gnome/xslt/docbook/utils/ids.xsl

_sklocalstatedir ?= `scrollkeeper-config --pkglocalstatedir`


################################################################################
## @@ Rules for rngdoc

rngdoc_args =									\
	--stringparam rngdoc.id							\
	$(shell echo $(basename $(notdir $(1))) | sed -e 's/[^A-Za-z0-9_-]/_/g')\
	$(_rngdoc) $(filter %/$(basename $(notdir $(1))).rng,$(_RNGDOC_RNGS))

## @ _RNGDOC_RNGS
## The actual RNG files for which to generate documentation with rngdoc
_RNGDOC_RNGS = $(sort $(patsubst ./%, %, $(foreach dir,$(RNGDOC_DIRS),		\
	$(wildcard $(dir)/*.rng) $(wildcard $(srcdir)/$(dir)/*.rng))))

## @ _RNGDOC_C_DOCS
## The generated rngdoc documentation in the C locale
_RNGDOC_C_DOCS = $(foreach rng,$(_RNGDOC_RNGS), C/$(basename $(notdir $(rng))).xml)

# FIXME: Fix the dependancies
$(_RNGDOC_C_DOCS) : $(_RNGDOC_RNGS)
	if ! test -d $(dir $@); then mkdir $(dir $@); fi;
	xsltproc $(call rngdoc_args,$@,$<) | xmllint --c14n - > $@.tmp && \
	  cp $@.tmp $@ && rm -f $@.tmp

.PHONY: rngdoc
rngdoc: $(_RNGDOC_C_DOCS)


################################################################################
## @@ Rules for xsldoc

# FIXME: _XSLDOC_XSLS is getting dupes with relative/absolute in some
# cases.  Right now, I'm just taking the first, but that's just a bad
# work-around.  Fix the real problem.
xsldoc_args =									\
	--stringparam xsldoc.id							\
	$(shell echo $(basename $(notdir $(1))) | sed -e 's/[^A-Za-z0-9_-]/_/g')\
	$(_xsldoc)								\
	$(word 1,$(filter %/$(basename $(notdir $(1))).xsl,$(_XSLDOC_XSLS)))

## @ _XSLDOC_XSLS
## The actual XSLT files for which to generate documentation with xsldoc
_XSLDOC_XSLS = $(sort $(patsubst ./%, %, $(foreach dir,$(XSLDOC_DIRS),		\
	$(wildcard $(dir)/*.xsl) $(wildcard $(srcdir)/$(dir)/*.xsl))))

## @ _XSLDOC_C_DOCS
## The generated xsldoc documentation in the C locale
_XSLDOC_C_DOCS = $(foreach xsl,$(_XSLDOC_XSLS), C/$(basename $(notdir $(xsl))).xml)

# FIXME: Fix the dependancies
$(_XSLDOC_C_DOCS) : $(_XSLDOC_XSLS)
	if ! test -d $(dir $@); then mkdir $(dir $@); fi;
	xsltproc $(call xsldoc_args,$@,$<) | xmllint --c14n - > $@.tmp && \
	  cp $@.tmp $@ && rm -f $@.tmp

.PHONY: xsldoc
xsldoc: $(_XSLDOC_C_DOCS)


################################################################################
## @@ Rules for OMF Files

db2omf_args =									\
	--stringparam db2omf.basename $(DOC_MODULE)				\
	--stringparam db2omf.format $(3)					\
	--stringparam db2omf.dtd						\
	$(shell xmllint --format $(2) | grep -h PUBLIC | head -n 1 		\
		| sed -e 's/.*PUBLIC \(\"[^\"]*\"\).*/\1/')			\
	--stringparam db2omf.lang $(notdir $(patsubst %/$(notdir $(2)),%,$(2)))	\
	--stringparam db2omf.omf_dir "$(OMF_DIR)"				\
	--stringparam db2omf.help_dir "$(HELP_DIR)"				\
	--stringparam db2omf.omf_in "`pwd`/$(_DOC_OMF_IN)"			\
	$(_db2omf) $(2)

## @ _DOC_OMF_IN
## The OMF input file
_DOC_OMF_IN = $(if $(DOC_MODULE),$(wildcard $(srcdir)/$(DOC_MODULE).omf.in))

## @ _DOC_OMF_DB
## The OMF files for DocBook output
_DOC_OMF_DB = $(if $(_DOC_OMF_IN),						\
	$(foreach lc,C $(_DOC_REAL_LINGUAS),$(DOC_MODULE)-$(lc).omf))

$(_DOC_OMF_DB) : $(_DOC_OMF_IN)
$(_DOC_OMF_DB) : $(DOC_MODULE)-%.omf : %/$(DOC_MODULE).xml
	xsltproc -o $@ $(call db2omf_args,$@,$<,'docbook')

## @ _DOC_OMF_HTML
## The OMF files for HTML output
_DOC_OMF_HTML = $(if $(_DOC_OMF_IN),						\
	$(foreach lc,C $(_DOC_REAL_LINGUAS),$(DOC_MODULE)-html-$(lc).omf))

$(_DOC_OMF_HTML) : $(_DOC_OMF_IN)
$(_DOC_OMF_HTML) : $(DOC_MODULE)-html-%.omf : %/$(DOC_MODULE).xml
	xsltproc -o $@ $(call db2omf_args,$@,$<,'xhtml')

## @ _DOC_OMF_ALL
## All OMF output files to be built
# FIXME
_DOC_OMF_ALL =									\
	$(if $(filter docbook,$(_DOC_REAL_FORMATS)),$(_DOC_OMF_DB))		\
	$(if $(filter html HTML,$(_DOC_REAL_FORMATS)),$(_DOC_OMF_HTML))

.PHONY: omf
omf: $(_DOC_OMF_ALL)


################################################################################
## @@ Rules for .cvsignore Files

## @ _CVSIGNORE_TOP
## The .cvsignore file in the top directory
_CVSIGNORE_TOP = $(if $(DOC_MODULE), .cvsignore)

## @ _CVSIGNORE_C
## The .cvsignore file in the C directory
_CVSIGNORE_C = $(if $(DOC_MODULE), C/.cvsignore)

## @ _CVSIGNORE_LC
## The .cvsignore files in other locale directories
_CVSIGNORE_LC = $(if $(DOC_MODULE),$(foreach lc,$(_DOC_REAL_LINGUAS),$(lc)/.cvsignore))

## @ _CVSIGNORE_TOP_FILES
## The list of files to be listed in the top-level .cvsignore file
_CVSIGNORE_TOP_FILES = $(_DOC_OMF_ALL) $(_DOC_DSK_ALL)

## @ _CVSIGNORE_C_FILES
## The list of files to be listed in the .cvsignore file in the C directory
_CVSIGNORE_C_FILES = $(_RNGDOC_C_DOCS) $(_XSLDOC_C_DOCS)

## @ _CVSIGNORE_C_FILES
## The list of files to be listed in the .cvsignore files in other
## locale directories
_CVSIGNORE_LC_FILES = $(_DOC_LC_DOCS)

$(_CVSIGNORE_TOP) : $(_CVSIGNORE_TOP_FILES)
	if ! test -f $@; then touch $@; fi
	cat $@ > $@.tmp
	list='$^'; for file in $$list; do \
	  echo $$file >> $@.tmp; \
	done
	cat $@.tmp | sort | uniq > $@
	rm $@.tmp

$(_CVSIGNORE_C) : $(_CVSIGNORE_C_FILES)
	if ! test -f $@; then touch $@; fi
	cat $@ > $@.tmp
	list='$^'; for file in $$list; do \
	  echo $$file | sed -e 's/.*\///' >> $@.tmp; \
	done
	cat $@.tmp | sort | uniq > $@
	rm $@.tmp

$(_CVSIGNORE_LC) : $(_CVSIGNORE_LC_FILES)
	if ! test -f $@; then touch $@; fi
	cat $@ > $@.tmp
	list='$(wildcard $(_CVSIGNORE_LC_FILES),$(dir $@)/*)'; \
	for file in $$list; do \
	  echo $$file | sed -e 's/.*\///' >> $@.tmp; \
	done
	cat $@.tmp | sort | uniq > $@
	rm $@.tmp

.PHONY: cvsignore
cvsignore: $(_CVSIGNORE_TOP) $(_CVSIGNROE_C) $(_CVSIGNORE_LC)


################################################################################
## @@ C Locale Documents

## @ _DOC_C_MODULE
## The top-level documentation file in the C locale
_DOC_C_MODULE = $(if $(DOC_MODULE),C/$(DOC_MODULE).xml)

## @ _DOC_C_ENTITIES
## Files included with a SYSTEM entity in the C locale
_DOC_C_ENTITIES = $(foreach ent,$(DOC_ENTITIES),C/$(ent))

## @ _DOC_C_XINCLUDES
## Files included with XInclude in the C locale
_DOC_C_INCLUDES = $(foreach inc,$(DOC_INCLUDES),C/$(inc))

## @ _DOC_C_DOCS
## All documentation files in the C locale
_DOC_C_DOCS =								\
	$(_DOC_C_ENTITIES)	$(_DOC_C_INCLUDES)			\
	$(_RNGDOC_C_DOCS)	$(_XSLDOC_C_DOCS)			\
	$(_DOC_C_MODULE)

## @ _DOC_C_DOCS_NOENT
## All documentation files in the C locale,
## except files included with a SYSTEM entity
_DOC_C_DOCS_NOENT =							\
	$(_DOC_C_MODULE)	$(_DOC_C_INCLUDES)			\
	$(_RNGDOC_C_DOCS)	$(_XSLDOC_C_DOCS)

## @ _DOC_C_FIGURES
## All figures and other external data in the C locale
_DOC_C_FIGURES = $(if $(DOC_FIGURES),					\
	$(foreach fig,$(DOC_FIGURES),C/$(fig)),				\
	$(patsubst $(srcdir)/%,%,$(wildcard $(srcdir)/C/figures/*.png)))

## @ _DOC_C_HTML
## All HTML documentation in the C locale
# FIXME: probably have to shell escape to determine the file names
_DOC_C_HTML = $(foreach f,						\
	$(shell xsltproc --xinclude 					\
	  --stringparam db.chunk.basename "$(DOC_MODULE)"		\
	  $(_chunks) "C/$(DOC_MODULE).xml"),				\
	C/$(f).xhtml)

###############################################################################
## @@ Other Locale Documentation

## @ _DOC_POFILES
## The .po files used for translating the document
_DOC_POFILES = $(if $(DOC_MODULE),						\
	$(foreach lc,$(_DOC_REAL_LINGUAS),$(lc)/$(lc).po))

.PHONY: po
po: $(_DOC_POFILES)

## @ _DOC_LC_MODULES
## The top-level documentation files in all other locales
_DOC_LC_MODULES = $(if $(DOC_MODULE),						\
	$(foreach lc,$(_DOC_REAL_LINGUAS),$(lc)/$(DOC_MODULE).xml))

## @ _DOC_LC_XINCLUDES
## Files included with XInclude in all other locales
_DOC_LC_INCLUDES =								\
	$(foreach lc,$(_DOC_REAL_LINGUAS),$(foreach inc,$(_DOC_C_INCLUDES),	\
		$(lc)/$(notdir $(inc)) ))

## @ _RNGDOC_LC_DOCS
## The generated rngdoc documentation in all other locales
_RNGDOC_LC_DOCS =								\
	$(foreach lc,$(_DOC_REAL_LINGUAS),$(foreach doc,$(_RNGDOC_C_DOCS),	\
		$(lc)/$(notdir $(doc)) ))

## @ _XSLDOC_LC_DOCS
## The generated xsldoc documentation in all other locales
_XSLDOC_LC_DOCS =								\
	$(foreach lc,$(_DOC_REAL_LINGUAS),$(foreach doc,$(_XSLDOC_C_DOCS),	\
		$(lc)/$(notdir $(doc)) ))

## @ _DOC_LC_HTML
## All HTML documentation in all other locales
# FIXME: probably have to shell escape to determine the file names
_DOC_LC_HTML =									\
	$(foreach lc,$(_DOC_REAL_LINGUAS),$(foreach doc,$(_DOC_C_HTML),		\
		$(lc)/$(notdir $(doc)) ))

## @ _DOC_LC_DOCS
## All documentation files in all other locales
_DOC_LC_DOCS =									\
	$(_DOC_LC_MODULES)	$(_DOC_LC_INCLUDES)				\
	$(_RNGDOC_LC_DOCS)	$(_XSLDOC_LC_DOCS)				\
	$(if $(filter html HTML,$(_DOC_REAL_FORMATS)),$(_DOC_LC_HTML))

## @ _DOC_LC_FIGURES
## All figures and other external data in all other locales
_DOC_LC_FIGURES = $(foreach lc,$(_DOC_REAL_LINGUAS),				\
	$(patsubst C/%,$(lc)/%,$(_DOC_C_FIGURES)) )

_DOC_SRC_FIGURES =								\
	$(foreach fig,$(_DOC_C_FIGURES), $(foreach lc,C $(_DOC_REAL_LINGUAS),	\
		$(wildcard $(srcdir)/$(lc)/$(patsubst C/%,%,$(fig))) ))

$(_DOC_POFILES):
	@if ! test -d $(dir $@); then \
	  echo "mkdir $(dir $@)"; \
	  mkdir "$(dir $@)"; \
	fi
	@if test ! -f $@ -a -f $(srcdir)/$@; then \
	  echo "cp $(srcdir)/$@ $@"; \
	  cp "$(srcdir)/$@" "$@"; \
	fi;
	@docs=; \
	list='$(_DOC_C_DOCS_NOENT)'; for doc in $$list; do \
	  if test -f $$doc; then \
	    docs="$$docs ../$$doc"; \
	  else \
	    docs="$$docs ../$(srcdir)/$$doc"; \
	  fi; \
	done; \
	if ! test -f $@; then \
	  echo "(cd $(dir $@) && \
	    $(_xml2po) -e $$docs > $(notdir $@).tmp && \
	    cp $(notdir $@).tmp $(notdir $@) && rm -f $(notdir $@).tmp)"; \
	  (cd $(dir $@) && \
	    $(_xml2po) -e $$docs > $(notdir $@).tmp && \
	    cp $(notdir $@).tmp $(notdir $@) && rm -f $(notdir $@).tmp); \
	else \
	  echo "(cd $(dir $@) && \
	    $(_xml2po) -e -u $(notdir $@) $$docs)"; \
	  (cd $(dir $@) && \
	    $(_xml2po) -e -u $(notdir $@) $$docs); \
	fi

# FIXME: fix the dependancy
# FIXME: hook xml2po up
$(_DOC_LC_DOCS) : $(_DOC_POFILES)
$(_DOC_LC_DOCS) : $(_DOC_C_DOCS)
	if ! test -d $(dir $@); then mkdir $(dir $@); fi
	case "$(srcdir)" in /*) sd="$(srcdir)";; *) sd="../$(srcdir)";;	esac; \
	if [ -f "C/$(notdir $@)" ]; then d="../"; else d="$$sd/"; fi; \
	(cd $(dir $@) && \
	  $(_xml2po) -e -p \
	    "$${d}$(dir $@)$(patsubst %/$(notdir $@),%,$@).po" \
	    "$${d}C/$(notdir $@)" > $(notdir $@).tmp && \
	    cp $(notdir $@).tmp $(notdir $@) && rm -f $(notdir $@).tmp)

## @ _DOC_POT
## A pot file
_DOC_POT = $(if $(DOC_MODULE),$(DOC_MODULE).pot)
.PHONY: pot
pot: $(_DOC_POT)
$(_DOC_POT): $(_DOC_C_DOCS_NOENT)
	$(_xml2po) -e -o $@ $^


################################################################################
## @@ All Documentation

## @ _DOC_HTML_ALL
## All HTML documentation, only if it's built
_DOC_HTML_ALL = $(if $(filter html HTML,$(_DOC_REAL_FORMATS)), \
	$(_DOC_C_HTML) $(_DOC_LC_HTML))

_DOC_HTML_TOPS = $(foreach lc,C $(_DOC_REAL_LINGUAS),$(lc)/$(DOC_MODULE).xhtml)

$(_DOC_HTML_TOPS): $(_DOC_C_DOCS) $(_DOC_LC_DOCS)
	xsltproc -o $@ --xinclude --param db.chunk.chunk_top "false()" --stringparam db.chunk.basename "$(DOC_MODULE)" --stringparam db.chunk.extension ".xhtml" $(_db2html) $(patsubst %.xhtml,%.xml,$@)


################################################################################

if ENABLE_SK
_ENABLE_SK = true
else
_ENABLE_SK = false
endif

all:							\
	$(_DOC_C_DOCS)		$(_DOC_LC_DOCS)		\
	$(_DOC_OMF_ALL)		$(_DOC_DSK_ALL)		\
	$(_DOC_HTML_ALL)	$(_DOC_POFILES)


.PHONY: clean-doc-rngdoc clean-doc-xsldoc clean-doc-omf clean-doc-dsk clean-doc-lc clean-doc-dir

clean-doc-rngdoc: ; rm -f $(_RNGDOC_C_DOCS) $(_RNGDOC_LC_DOCS)
clean-doc-xsldoc: ; rm -f $(_XSLDOC_C_DOCS) $(_XSLDOC_LC_DOCS)
clean-doc-omf: ; rm -f $(_DOC_OMF_DB) $(_DOC_OMF_HTML)
clean-doc-dsk: ; rm -f $(_DOC_DSK_DB) $(_DOC_DSK_HTML)
clean-doc-lc:
	rm -f $(_DOC_LC_DOCS)
	@list='$(_DOC_POFILES)'; for po in $$list; do \
	  if ! test "$$po" -ef "$(srcdir)/$$po"; then \
	    echo "rm -f $$po"; \
	    rm -f "$$po"; \
	  fi; \
	done
	@for lc in C $(_DOC_REAL_LINGUAS); do \
	  if test -f "$$lc/.xml2po.mo"; then \
	    echo "rm -f $$lc/.xml2po.mo"; \
	    rm -f "$$lc/.xml2po.mo"; \
	  fi; \
	done
clean-doc-dir:
	@for lc in C $(_DOC_REAL_LINGUAS); do \
	  for dir in `find $$lc -depth -type d`; do \
	    if ! test $$dir -ef $(srcdir)/$$dir; then \
	      echo "rmdir $$dir"; \
	      rmdir "$$dir"; \
	   fi; \
	  done; \
	done

_clean_rngdoc = $(if $(RNGDOC_DIRS),clean-doc-rngdoc)
_clean_xsldoc = $(if $(XSLDOC_DIRS),clean-doc-xsldoc)
_clean_omf = $(if $(_DOC_OMF_IN),clean-doc-omf)
_clean_dsk = $(if $(_DOC_DSK_IN),clean-doc-dsk)
_clean_lc  = $(if $(_DOC_REAL_LINGUAS),clean-doc-lc)
_clean_dir = $(if $(DOC_MODULE),clean-doc-dir)

clean-local:						\
	$(_clean_rngdoc)	$(_clean_xsldoc)	\
	$(_clean_omf)		$(_clean_dsk)		\
	$(_clean_lc)		$(_clean_dir)
distclean-local:					\
	$(_clean_rngdoc)	$(_clean_xsldoc)	\
	$(_clean_omf)		$(_clean_dsk)		\
	$(_clean_lc)		$(_clean_dir)
mostlyclean-local:					\
	$(_clean_rngdoc)	$(_clean_xsldoc)	\
	$(_clean_omf)		$(_clean_dsk)		\
	$(_clean_lc)		$(_clean_dir)
maintainer-clean-local:					\
	$(_clean_rngdoc)	$(_clean_xsldoc)	\
	$(_clean_omf)		$(_clean_dsk)		\
	$(_clean_lc)		$(_clean_dir)


.PHONY: dist-doc-docs dist-doc-figs dist-doc-omf dist-doc-dsk
doc-dist-hook: 					\
	$(if $(DOC_MODULE),dist-doc-docs)	\
	$(if $(_DOC_C_FIGURES),dist-doc-figs)	\
	$(if $(_DOC_OMF_IN),dist-doc-omf)
#	$(if $(_DOC_DSK_IN),dist-doc-dsk)

dist-doc-docs: $(_DOC_C_DOCS) $(_DOC_LC_DOCS) $(_DOC_POFILES)
	@for lc in C $(_DOC_REAL_LINGUAS); do \
	  echo " $(mkinstalldirs) $(distdir)/$$lc"; \
	  $(mkinstalldirs) "$(distdir)/$$lc"; \
	done
	@list='$(_DOC_C_DOCS) $(_DOC_LC_DOCS) $(_DOC_POFILES)'; \
	for doc in $$list; do \
	  if test -f "$$doc"; then d=; else d="$(srcdir)/"; fi; \
	  echo "$(INSTALL_DATA) $$d$$doc $(distdir)/$$doc"; \
	  $(INSTALL_DATA) "$$d$$doc" "$(distdir)/$$doc"; \
	done

dist-doc-figs: $(_DOC_SRC_FIGURES)
	@list='$(_DOC_C_FIGURES) $(_DOC_LC_FIGURES)'; \
	for fig in $$list; do \
	  if test -f "$$fig"; then d=; else d="$(srcdir)/"; fi; \
	  if test -f "$$d$$fig"; then \
	    figdir=`echo $$fig | sed -e 's/^\(.*\/\).*/\1/' -e '/\//!s/.*//'`; \
	    if ! test -d "$(distdir)/$$figdir"; then \
	      echo "$(mkinstalldirs) $(distdir)/$$figdir"; \
	      $(mkinstalldirs) "$(distdir)/$$figdir"; \
	    fi; \
	    echo "$(INSTALL_DATA) $$d$$fig $(distdir)/$$fig"; \
	    $(INSTALL_DATA) "$$d$$fig" "$(distdir)/$$fig"; \
	  fi; \
	done;

dist-doc-omf:
	@if test -f "$(_DOC_OMF_IN)"; then d=; else d="$(srcdir)/"; fi; \
	echo "$(INSTALL_DATA) $$d$(_DOC_OMF_IN) $(distdir)/$(notdir $(_DOC_OMF_IN))"; \
	$(INSTALL_DATA) "$$d$(_DOC_OMF_IN)" "$(distdir)/$(notdir $(_DOC_OMF_IN))"

dist-doc-dsk:
	@if test -f "$(_DOC_DSK_IN)"; then d=; else d="$(srcdir)/"; fi; \
	echo "$(INSTALL_DATA) $$d$(_DOC_DSK_IN) $(distdir)/$(notdir $(_DOC_DSK_IN))"; \
	$(INSTALL_DATA) "$$d$(_DOC_DSK_IN)" "$(distdir)/$(notdir $(_DOC_DSK_IN))"


.PHONY: check-doc-docs check-doc-omf
check:							\
	$(if $(DOC_MODULE),check-doc-docs)		\
	$(if $(_DOC_OMF_IN),check-doc-omf)

check-doc-docs: $(_DOC_C_DOCS) $(_DOC_LC_DOCS)
	@for lc in C $(_DOC_REAL_LINGUAS); do \
	  if test -f "$$lc"; \
	    then d=; \
	    xmlpath="$$lc"; \
	  else \
	    d="$(srcdir)/"; \
	    xmlpath="$$lc:$(srcdir)/$$lc"; \
	  fi; \
	  echo "xmllint --noout --noent --path $$xmlpath --xinclude --postvalid $$d$$lc/$(DOC_MODULE).xml"; \
	  xmllint --noout --noent --path "$$xmlpath" --xinclude --postvalid "$$d$$lc/$(DOC_MODULE).xml"; \
	done

check-doc-omf: $(_DOC_OMF_ALL)
	@list='$(_DOC_OMF_ALL)'; for omf in $$list; do \
	  echo "xmllint --noout --dtdvalid 'http://scrollkeeper.sourceforge.net/dtds/scrollkeeper-omf-1.0/scrollkeeper-omf.dtd' $$omf"; \
	  xmllint --noout --dtdvalid 'http://scrollkeeper.sourceforge.net/dtds/scrollkeeper-omf-1.0/scrollkeeper-omf.dtd' $$omf; \
	done


.PHONY: install-doc-docs install-doc-html install-doc-figs install-doc-omf install-doc-dsk
install-data-local:					\
	$(if $(DOC_MODULE),install-doc-docs)		\
	$(if $(_DOC_HTML_ALL),install-doc-html)		\
	$(if $(_DOC_C_FIGURES),install-doc-figs)	\
	$(if $(_DOC_OMF_IN),install-doc-omf)
#	$(if $(_DOC_DSK_IN),install-doc-dsk)

install-doc-docs:
	@for lc in C $(_DOC_REAL_LINGUAS); do \
	  echo "$(mkinstalldirs) $(DESTDIR)$(HELP_DIR)/$(DOC_MODULE)/$$lc"; \
	  $(mkinstalldirs) $(DESTDIR)$(HELP_DIR)/$(DOC_MODULE)/$$lc; \
	done
	@list='$(_DOC_C_DOCS) $(_DOC_LC_DOCS)'; for doc in $$list; do \
	  if test -f "$$doc"; then d=; else d="$(srcdir)/"; fi; \
	  echo "$(INSTALL_DATA) $$d$$doc $(DESTDIR)$(HELP_DIR)/$(DOC_MODULE)/$$doc"; \
	  $(INSTALL_DATA) $$d$$doc $(DESTDIR)$(HELP_DIR)/$(DOC_MODULE)/$$doc; \
	done

install-doc-figs:
	@list='$(patsubst C/%,%,$(_DOC_C_FIGURES))'; for fig in $$list; do \
	  for lc in C $(_DOC_REAL_LINGUAS); do \
	    if test -f "$$lc/$$fig"; then \
	      figfile="$$lc/$$fig"; \
	    elif test -f "$(srcdir)/$$lc/$$fig"; then \
	      figfile="$(srcdir)/$$lc/$$fig"; \
	    elif test -f "C/$$fig"; then \
	      figfile="C/$$fig"; \
	    else \
	      figfile="$(srcdir)/C/$$fig"; \
	    fi; \
	    figdir="$$lc/"`echo $$fig | sed -e 's/^\(.*\/\).*/\1/' -e '/\//!s/.*//'`; \
	    figdir="$(DESTDIR)$(HELP_DIR)/$(DOC_MODULE)/$$figdir"; \
	    if ! test -d "$$figdir"; then \
	      echo "$(mkinstalldirs) $$figdir"; \
	      $(mkinstalldirs) "$$figdir"; \
	    fi; \
	    figbase=`echo $$fig | sed -e 's/^.*\///'`; \
	    echo "$(INSTALL_DATA) $$figfile $$figdir$$figbase"; \
	    $(INSTALL_DATA) "$$figfile" "$$figdir$$figbase"; \
	  done; \
	done

install-doc-html:
	echo install-html

install-doc-omf:
	$(mkinstalldirs) $(DESTDIR)$(OMF_DIR)/$(DOC_MODULE)
	@list='$(_DOC_OMF_ALL)'; for omf in $$list; do \
	  echo "$(INSTALL_DATA) $$omf $(DESTDIR)$(OMF_DIR)/$(DOC_MODULE)/$$omf"; \
	  $(INSTALL_DATA) $$omf $(DESTDIR)$(OMF_DIR)/$(DOC_MODULE)/$$omf; \
	done
	@if test "x$(_ENABLE_SK)" = "xtrue"; then \
	  echo "scrollkeeper-update -p $(DESTDIR)$(_sklocalstatedir) -o $(DESTDIR)$(OMF_DIR)/$(DOC_MODULE)"; \
	  scrollkeeper-update -p "$(DESTDIR)$(_sklocalstatedir)" -o "$(DESTDIR)$(OMF_DIR)/$(DOC_MODULE)"; \
	fi;

install-doc-dsk:
	echo install-dsk


.PHONY: uninstall-doc-docs uninstall-doc-html uninstall-doc-figs uninstall-doc-omf uninstall-doc-dsk
uninstall-local:					\
	$(if $(DOC_MODULE),uninstall-doc-docs)		\
	$(if $(_DOC_HTML_ALL),uninstall-doc-html)	\
	$(if $(_DOC_C_FIGURES),uninstall-doc-figs)	\
	$(if $(_DOC_OMF_IN),uninstall-doc-omf)
#	$(if $(_DOC_DSK_IN),uninstall-doc-dsk)

uninstall-doc-docs:
	@list='$(_DOC_C_DOCS) $(_DOC_LC_DOCS)'; for doc in $$list; do \
	  echo " rm -f $(DESTDIR)$(HELP_DIR)/$(DOC_MODULE)/$$doc"; \
	  rm -f "$(DESTDIR)$(HELP_DIR)/$(DOC_MODULE)/$$doc"; \
	done

uninstall-doc-figs:
	@list='$(_DOC_C_FIGURES) $(_DOC_LC_FIGURES)'; for fig in $$list; do \
	  echo "rm -f $(DESTDIR)$(HELP_DIR)/$(DOC_MODULE)/$$fig"; \
	  rm -f "$(DESTDIR)$(HELP_DIR)/$(DOC_MODULE)/$$fig"; \
	done;

uninstall-doc-omf:
	@list='$(_DOC_OMF_ALL)'; for omf in $$list; do \
	  if test "x$(_ENABLE_SK)" == "xtrue"; then \
	    echo "scrollkeeper-uninstall -p $(_sklocalstatedir) $(DESTDIR)$(OMF_DIR)/$(DOC_MODULE)/$$omf"; \
	    scrollkeeper-uninstall -p "$(_sklocalstatedir)" "$(DESTDIR)$(OMF_DIR)/$(DOC_MODULE)/$$omf"; \
	  fi; \
	  echo "rm -f $(DESTDIR)$(OMF_DIR)/$(DOC_MODULE)/$$omf"; \
	  rm -f "$(DESTDIR)$(OMF_DIR)/$(DOC_MODULE)/$$omf"; \
	done
