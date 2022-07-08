VERSION := 0.15

GIT := $(shell command -v git 2> /dev/null)

ifdef GIT
GIT_COMMIT   := $(shell $(GIT) describe --always --dirty=-modified)
LONG_VERSION := "$(VERSION) ($(GIT_COMMIT))"
else
LONG_VERSION := $(VERSION)
endif
