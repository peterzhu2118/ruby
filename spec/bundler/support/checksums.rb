# frozen_string_literal: true

module Spec
  module Checksums
    class ChecksumsBuilder
      def initialize(&block)
        @checksums = {}
        yield self if block_given?
      end

      def repo_gem(repo, name, version, platform = Gem::Platform::RUBY)
        gem_file = File.join(repo, "gems", "#{Bundler::GemHelpers.spec_full_name(name, version, platform)}.gem")
        File.open(gem_file, "rb") do |f|
          checksums = Bundler::Checksum.from_io(f, "ChecksumsBuilder")
          checksum_entry(checksums, name, version, platform)
        end
      end

      def no_checksum(name, version, platform = Gem::Platform::RUBY)
        checksum_entry(nil, name, version, platform)
      end

      def checksum_entry(checksums, name, version, platform = Gem::Platform::RUBY)
        lock_name = Bundler::GemHelpers.lock_name(name, version, platform)
        @checksums[lock_name] = checksums
      end

      def to_lock
        @checksums.map do |lock_name, checksums|
          checksums &&= " #{checksums.map(&:to_lock).join(",")}"
          "  #{lock_name}#{checksums}\n"
        end.sort.join.strip
      end
    end

    def checksum_section(&block)
      ChecksumsBuilder.new(&block).to_lock
    end

    def checksum_for_repo_gem(*args)
      checksum_section do |c|
        c.repo_gem(*args)
      end
    end

    def gem_no_checksum(*args)
      checksum_section do |c|
        c.no_checksum(*args)
      end
    end

    # if prefixes is given, removes all checksums where the line
    # has any of the prefixes on the line before the checksum
    # otherwise, removes all checksums from the lockfile
    def remove_checksums_from_lockfile(lockfile, *prefixes)
      head, remaining = lockfile.split(/^CHECKSUMS$/, 2)
      checksums, tail = remaining.split("\n\n", 2)

      prefixes =
        if prefixes.empty?
          nil
        else
          /(#{prefixes.map {|p| Regexp.escape(p) }.join("|")})/
        end

      checksums = checksums.each_line.map do |line|
        if prefixes.nil? || line.match?(prefixes)
          line.gsub(/ sha256-[a-f0-9]{64}/i, "")
        else
          line
        end
      end

      head.concat(
        "CHECKSUMS",
        checksums.join,
        "\n\n",
        tail
      )
    end
  end
end