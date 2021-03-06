# Copyright(C) 2010-2012 Ruby-GNOME2 Project.
#
# This program is licenced under the same license of Ruby-GNOME2.

require 'open-uri'
require 'rubygems'
require 'mechanize'

class GNOME2WindowsBinaryDownloadTask
  include Rake::DSL

  URL_BASE = "http://ftp.gnome.org/pub/gnome/binaries"
  def initialize(package)
    @package = package
    define
  end

  private
  def define
    namespace :windows do
      namespace :downloader do
        task :before

        download_tasks = []
        namespace :download do
          directory dist_dir.to_s
          task :prepare => [dist_dir.to_s]

          packages.each do |package|
            desc "download #{package}"
            task package => [:prepare] do
              download_package(package)
            end
            download_tasks << package
          end

          dependencies.each do |dependency|
            name, version = dependency
            desc "download #{name}"
            task name => [:prepare] do
              download_dependency(dependency)
            end
            download_tasks << name
          end
        end

        download_tasks = download_tasks.collect do |task|
          "windows:downloader:download:#{task}"
        end
        desc "download Windows binaries into #{dist_dir}"
        task :download => download_tasks

        task :after
      end
      desc "download Windows binaries"
      task :download => ["windows:downloader:before",
                         "windows:downloader:download",
                         "windows:downloader:after"]
    end
  end

  def dist_dir
    @package.windows.absolute_binary_dir
  end

  def packages
    @package.windows.packages
  end

  def dependencies
    @package.windows.dependencies
  end

  def build_architecture_suffix
    @package.windows.build_architecture_suffix
  end

  def download_package(package)
    suffix = build_architecture_suffix
    version_page_url = "#{URL_BASE}/#{suffix}/#{package}"
    version_page = agent.get(version_page_url)
    latest_version_link = version_page.links.sort_by do |link|
      if /\A(\d+\.\d+)\/\z/ =~ link.href
        $1.split(/\./).collect {|component| component.to_i}
      else
        [-1]
      end
    end.last

    escaped_package = Regexp.escape(package)
    latest_version_page = latest_version_link.click
    latest_version = latest_version_page.links.collect do |link|
      case link.href
      when /#{escaped_package}_([\d\.\-]+)_#{suffix}\.zip\z/,
           /#{escaped_package}-([\d\.\-]+)-#{suffix}\.zip\z/, # old
           /#{escaped_package}-([\d\.\-]+)\.zip\z/ # old
        version = $1
        normalized_version = version.split(/[\.\-]/).collect do |component|
          component.to_i
        end
        [normalized_version, version]
      else
        [[-1], nil]
      end
    end.sort_by do |normalized_version, version|
      normalized_version
    end.last[1]

    if latest_version.nil?
      raise "can't find package: <#{package}>:<#{version_page_url}>"
    end
    escaped_latest_version = Regexp.escape(latest_version)
    latest_version_page.links.each do |link|
      case link.href
      when /#{escaped_package}(?:-dev)?_#{escaped_latest_version}_#{suffix}\.zip\z/,
           /#{escaped_package}(?:-dev)?-#{escaped_latest_version}-#{suffix}\.zip\z/, # old
           /#{escaped_package}(?:-dev)?-#{escaped_latest_version}\.zip\z/ # old
        click_zip_link(link)
      end
    end
  end

  def download_dependency(dependency)
    suffix = build_architecture_suffix
    dependency_version = "any"
    dependency_version_re = /[\d\.\-]+/
    if dependency.is_a?(Array)
      dependency, dependency_version = dependency
      dependency_version_re = /#{Regexp.escape(dependency_version)}/
    end
    escaped_dependency = Regexp.escape(dependency)
    dependencies_url = "#{URL_BASE}/dependencies"
    dependencies_page = agent.get(dependencies_url)
    latest_version = dependencies_page.links.collect do |link|
      case link.href
      when /\A#{escaped_dependency}_(#{dependency_version_re})_#{suffix}\.zip\z/
        version = $1
        [version.split(/[\.\-]/).collect {|component| component.to_i}, version]
      else
        [[-1], nil]
      end
    end.sort_by do |normalized_version, version|
      normalized_version
    end.last[1]

    if latest_version.nil?
      message = "can't find dependency package: " +
        "<#{dependency}>(#{dependency_version}):<#{dependencies_url}>"
      raise message
    end
    escaped_latest_version = Regexp.escape(latest_version)
    dependencies_page.links.each do |link|
      case link.href
      when /\A#{escaped_dependency}(?:-dev)?_#{escaped_latest_version}_#{suffix}\.zip\z/
        click_zip_link(link)
      end
    end
  end

  private
  def agent
    @agent ||= Mechanize.new
  end

  def click_zip_link(link)
    zip = link.click
    Dir.chdir(dist_dir) do
      open(zip.filename, "wb") do |file|
        file.print(zip.body)
      end
      system("unzip", "-o", zip.filename)
      Dir.glob("lib/pkgconfig/*.pc") do |pc_path|
        pc = File.read(pc_path)
        pc = pc.gsub(/\Aprefix=.+$/) {"prefix=#{dist_dir}"}
        File.open(pc_path, "w") do |pc_file|
          pc_file.print(pc)
        end
      end
    end
  end
end
