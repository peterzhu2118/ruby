# frozen_string_literal: true

require "tempfile"
require "rubygems"
require "rubygems/remote_fetcher"

##
# A fake Gem::RemoteFetcher for use in tests or to avoid real live HTTP
# requests when testing code that uses RubyGems.
#
# Example:
#
#   @fetcher = Gem::FakeFetcher.new
#   @fetcher.data['http://gems.example.com/yaml'] = source_index.to_yaml
#   Gem::RemoteFetcher.fetcher = @fetcher
#
#   use nested array if multiple response is needed
#
#   @fetcher.data['http://gems.example.com/sequence'] = [['Success', 200, 'OK'], ['Failed', 401, 'Unauthorized']]
#
#   @fetcher.fetch_path('http://gems.example.com/sequence') # => ['Success', 200, 'OK']
#   @fetcher.fetch_path('http://gems.example.com/sequence') # => ['Failed', 401, 'Unauthorized']
#
#   # invoke RubyGems code
#
#   paths = @fetcher.paths
#   assert_equal 'http://gems.example.com/yaml', paths.shift
#   assert paths.empty?, paths.join(', ')
#
# See RubyGems' tests for more examples of FakeFetcher.

class Gem::FakeFetcher
  attr_reader :data, :requests
  attr_accessor :paths

  def initialize
    @data = {}
    @paths = []
    @requests = []
  end

  def find_data(path)
    return Gem.read_binary path.path if Gem::URI === path && path.scheme == "file"

    if Gem::URI === path && "Gem::URI::#{path.scheme.upcase}" != path.class.name
      raise ArgumentError,
        "mismatch for scheme #{path.scheme} and class #{path.class}"
    end

    path = path.to_s
    @paths << path
    raise ArgumentError, "need full Gem::URI" unless path.start_with?("https://", "http://")

    unless @data.key? path
      raise Gem::RemoteFetcher::FetchError.new("no data for #{path}", path)
    end

    if @data[path].is_a?(Array)
      @data[path].shift
    else
      @data[path]
    end
  end

  def create_response(uri)
    data = find_data(uri)
    response = data.respond_to?(:call) ? data.call : data
    raise TypeError, "#{response.class} is not a type of Gem::Net::HTTPResponse" unless response.is_a?(Gem::Net::HTTPResponse)

    response
  end

  def fetch_path(path, mtime = nil, head = false)
    data = find_data(path)

    if data.respond_to?(:call)
      data.call
    else
      if path.to_s.end_with?(".gz") && !data.nil? && !data.empty?
        data = Gem::Util.gunzip data
      end
      data
    end
  end

  def cache_update_path(uri, path = nil, update = true)
    if data = fetch_path(uri)
      File.open(path, "wb") {|io| io.write data } if path && update
      data
    else
      Gem.read_binary(path) if path
    end
  end

  # Thanks, FakeWeb!
  def open_uri_or_path(path)
    find_data(path)

    create_response(uri)
  end

  def last_request
    @requests.last
  end

  class FakeSocket < StringIO
    def continue_timeout
      1
    end
  end

  def request(uri, request_class, last_modified = nil)
    @requests << request_class.new(uri.request_uri)
    yield last_request if block_given?

    # Ensure multipart request bodies are generated
    socket = FakeSocket.new
    last_request.content_type ||= "application/x-www-form-urlencoded"
    last_request.exec socket.binmode, "1.1", last_request.path
    _, last_request.body = socket.string.split("\r\n\r\n", 2)

    create_response(uri)
  end

  def pretty_print(q) # :nodoc:
    q.group 2, "[FakeFetcher", "]" do
      q.breakable
      q.text "URIs:"

      q.breakable
      q.pp @data.keys
    end
  end

  def fetch_size(path)
    path = path.to_s
    @paths << path

    raise ArgumentError, "need full URI" unless %r{^http://}.match?(path)

    unless @data.key? path
      raise Gem::RemoteFetcher::FetchError.new("no data for #{path}", path)
    end

    data = @data[path]

    data.respond_to?(:call) ? data.call : data.length
  end

  def download(spec, source_uri, install_dir = Gem.dir)
    name = File.basename spec.cache_file
    path = if Dir.pwd == install_dir # see fetch_command
      install_dir
    else
      File.join install_dir, "cache"
    end

    path = File.join path, name

    if /^http/.match?(source_uri)
      File.open(path, "wb") do |f|
        f.write fetch_path(File.join(source_uri, "gems", name))
      end
    else
      FileUtils.cp source_uri, path
    end

    path
  end

  def download_to_cache(dependency)
    found, _ = Gem::SpecFetcher.fetcher.spec_for_dependency dependency

    return if found.empty?

    spec, source = found.first

    download spec, source.uri.to_s
  end
end

##
# The HTTPResponseFactory allows easy creation of Gem::Net::HTTPResponse instances in RubyGems tests:
#
# Example:
#
#   Gem::HTTPResponseFactory.create(
#     body: "",
#     code: 301,
#     msg: "Moved Permanently",
#     headers: { "location" => "http://example.com" }
#   )
#

class Gem::HTTPResponseFactory
  def self.create(body:, code:, msg:, headers: {})
    response = Gem::Net::HTTPResponse.send(:response_class, code.to_s).new("1.0", code.to_s, msg)
    response.instance_variable_set(:@body, body)
    response.instance_variable_set(:@read, true)
    headers.each {|name, value| response[name] = value }

    response
  end
end

##
# A Gem::MockBrowser is used in tests to mock a browser in that it can
# send HTTP requests to the defined URI.
#
# Example:
#
#   # Sends a get request to http://localhost:5678
#   Gem::MockBrowser.get Gem::URI("http://localhost:5678")
#
# See RubyGems' tests for more examples of MockBrowser.
#

class Gem::MockBrowser
  def self.options(uri)
    options = Gem::Net::HTTP::Options.new(uri)
    Gem::Net::HTTP.start(uri.hostname, uri.port) do |http|
      http.request(options)
    end
  end

  def self.get(uri)
    get = Gem::Net::HTTP::Get.new(uri)
    Gem::Net::HTTP.start(uri.hostname, uri.port) do |http|
      http.request(get)
    end
  end

  def self.post(uri, content_type: "application/x-www-form-urlencoded")
    headers = { "content-type" => content_type } if content_type
    post = Gem::Net::HTTP::Post.new(uri, headers)
    Gem::Net::HTTP.start(uri.hostname, uri.port) do |http|
      http.request(post)
    end
  end
end

# :stopdoc:
class Gem::RemoteFetcher
  def self.fetcher=(fetcher)
    @fetcher = fetcher
  end
end
# :startdoc:

##
# The SpecFetcherSetup allows easy setup of a remote source in RubyGems tests:
#
#   spec_fetcher do |f|
#     f.gem  'a', 1
#     f.spec 'a', 2
#     f.gem  'b', 1' 'a' => '~> 1.0'
#   end
#
# The above declaration creates two gems, a-1 and b-1, with a dependency from
# b to a.  The declaration creates an additional spec a-2, but no gem for it
# (so it cannot be installed).
#
# After the gems are created they are removed from Gem.dir.

class Gem::TestCase::SpecFetcherSetup
  ##
  # Executes a SpecFetcher setup block.  Yields an instance then creates the
  # gems and specifications defined in the instance.

  def self.declare(test, repository)
    setup = new test, repository

    yield setup

    setup.execute
  end

  def initialize(test, repository) # :nodoc:
    @test       = test
    @repository = repository

    @gems       = {}
    @downloaded = []
    @installed  = []
    @operations = []
  end

  ##
  # Returns a Hash of created Specification full names and the corresponding
  # Specification.

  def created_specs
    created = {}

    @gems.keys.each do |spec|
      created[spec.full_name] = spec
    end

    created
  end

  ##
  # Creates any defined gems or specifications

  def execute # :nodoc:
    execute_operations

    setup_fetcher

    created_specs
  end

  def execute_operations # :nodoc:
    @operations.each do |operation, *arguments|
      block = arguments.pop
      case operation
      when :gem then
        spec, gem = @test.util_gem(*arguments, &block)

        write_spec spec

        @gems[spec] = gem
        @installed << spec
      when :download then
        spec, gem = @test.util_gem(*arguments, &block)

        @gems[spec] = gem
        @downloaded << spec
      when :spec then
        spec = @test.util_spec(*arguments, &block)

        write_spec spec

        @gems[spec] = nil
        @installed << spec
      end
    end
  end

  ##
  # Creates a gem with +name+, +version+ and +deps+.  The created gem can be
  # downloaded and installed.
  #
  # The specification will be yielded before gem creation for customization,
  # but only the block or the dependencies may be set, not both.

  def gem(name, version, dependencies = nil, &block)
    @operations << [:gem, name, version, dependencies, block]
  end

  ##
  # Creates a gem with +name+, +version+ and +deps+.  The created gem is
  # downloaded in to the cache directory but is not installed
  #
  # The specification will be yielded before gem creation for customization,
  # but only the block or the dependencies may be set, not both.

  def download(name, version, dependencies = nil, &block)
    @operations << [:download, name, version, dependencies, block]
  end

  ##
  # Creates a legacy platform spec with the name 'pl' and version 1

  def legacy_platform
    spec "pl", 1 do |s|
      s.platform = Gem::Platform.new "i386-linux"
      s.instance_variable_set :@original_platform, "i386-linux"
    end
  end

  def setup_fetcher # :nodoc:
    require "zlib"
    require "socket"
    require "rubygems/remote_fetcher"

    unless @test.fetcher
      @test.fetcher = Gem::FakeFetcher.new
      Gem::RemoteFetcher.fetcher = @test.fetcher
    end

    Gem::Specification.reset

    begin
      gem_repo = @test.gem_repo
      @test.gem_repo = @repository
      @test.uri = Gem::URI @repository

      @test.util_setup_spec_fetcher(*@downloaded)
    ensure
      @test.gem_repo = gem_repo
      @test.uri = Gem::URI gem_repo
    end

    @gems.each do |spec, gem|
      next unless gem

      @test.fetcher.data["#{@repository}gems/#{spec.file_name}"] =
        Gem.read_binary(gem)

      FileUtils.cp gem, spec.cache_file
    end
  end

  ##
  # Creates a spec with +name+, +version+ and +deps+.  The created gem can be
  # downloaded and installed.
  #
  # The specification will be yielded before creation for customization,
  # but only the block or the dependencies may be set, not both.

  def spec(name, version, dependencies = nil, &block)
    @operations << [:spec, name, version, dependencies, block]
  end

  def write_spec(spec) # :nodoc:
    File.open spec.spec_file, "w" do |io|
      io.write spec.to_ruby_for_cache
    end
  end
end

##
# A StringIO duck-typed class that uses Tempfile instead of String as the
# backing store.
#
# This class was added to flush out problems in Rubinius' IO implementation.

class Gem::TempIO < Tempfile
  ##
  # Creates a new TempIO that will be initialized to contain +string+.

  def initialize(string = "")
    super "TempIO"
    binmode
    write string
    rewind
  end

  ##
  # The content of the TempIO as a String.

  def string
    flush
    Gem.read_binary path
  end
end

class Gem::TestCase
  TempIO = Gem::TempIO
  HTTPResponseFactory = Gem::HTTPResponseFactory
end
