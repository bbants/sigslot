#pragma once


#include <memory>
#include <functional>
#include <list>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <string>
#include <map>

namespace nsNamedSigslot
{
	template<typename Signature, typename Mutex = std::recursive_mutex>
	class Signal;
	template<typename Mutex = std::recursive_mutex>
	class Signals;

	class ConnectionBase
	{
	protected:
		std::atomic<bool> enable_ = true;
		std::string name_;
		std::string sig_name_;
	public:
		virtual ~ConnectionBase(){}
		void Enable(bool enable=true){enable_ = enable;}
		bool Enabled(){return enable_;}
		std::string Name(){ return name_; }
		std::string SigName(){ return sig_name_; }
	};

	template<typename Signature>
	class Connection :
		public ConnectionBase
	{
		friend class Signal<Signature>;
		std::function<Signature> slot_;

		template <typename ...Params>
		void operator()(Params&&... params)
		{
			if (!Enabled())
				return;
			slot_(std::forward<Params>(params)...);
		}
		template <typename ...Params>
		void Emit(Params&&... params)
		{
			operator()(std::forward<Params>(params)...);
		}
	};

	class SignalBase
	{
	protected:
		std::atomic<bool> enable_ = true;
		std::string name_;
	public:
		virtual ~SignalBase(){}
		void Enable(bool enable=true){ enable_ = enable; }
		bool Enabled(){ return enable_; }
		std::string Name(){ return name_; }
	};

	template<typename Signature, typename Mutex>
	class Signal :
		public SignalBase
	{
		friend class Signals<Mutex>;
		Mutex lock_;
		std::list<std::weak_ptr<Connection<Signature>>> conns_;
	public:
		template <typename ...Params>
		void operator()(Params&&... params)
		{
			if (!Enabled())
				return;

			std::lock_guard<decltype(lock_)> l(lock_);
			auto it = conns_.begin();
			auto itEnd = conns_.end();

			while (it != itEnd)
			{
				auto itNext = it;
				++itNext;

				auto conn = it->lock();
				if (conn)
					(*conn)(std::forward<Params>(params)...);
				else
					conns_.erase(it);

				it = itNext;
			}
		}
		template <typename ...Params>
		void Emit(Params&&... params)
		{
			operator()(std::forward<Params>(params)...);
		}
		// save the return value as long as you want to keep the connection
		auto Connect(std::function<Signature> func, std::string name = "") -> std::shared_ptr<Connection<Signature>>
		{
			auto conn = std::make_shared<Connection<Signature>>();
			conn->slot_ = func;
			conn->name_ = name;
			conn->sig_name_ = name_;

			std::lock_guard<decltype(lock_)> l(lock_);
			conns_.push_back(conn);
			return conn;
		}
		// this is not required, you can reset conn to disconnect
		void Disconnect(std::shared_ptr<Connection<Signature>> conn)
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			auto iter = std::find_if(conns_.begin(), conns_.end(), [&conn](std::weak_ptr<Connection<Signature>> l){
				auto lconn = l.lock();
				return (lconn == conn);
			});
			if (conns_.end() != iter)
				conns_.erase(iter);
		}
		void DisconnectAll()
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			conns_.clear();
		}
	};
	// a utility class to hold all connections
	template<typename Mutex = std::recursive_mutex>
	class Connections
	{
		Mutex lock_;
		std::list<std::shared_ptr<ConnectionBase>> conns_;
	public:
		void Save(std::shared_ptr<ConnectionBase> conn)
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			conns_.push_back(conn);
		}
		void Enable(bool enable=true)
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			for (auto&& slot : conns_)
				slot->Enable(enable);
		}
		template<typename Comp>
		void EnableIf(Comp comp, bool enable=true)
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			for (auto&& slot : conns_)
			{
				if (comp(slot))
				{
					slot->Enable(enable);
				}
			}
		}
	};

	template<typename Mutex>
	class Signals
	{
		Mutex lock_;
		std::map<std::string, std::weak_ptr<SignalBase>> signals_;
	public:
		Signals()
		{}
		~Signals()
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			signals_.clear();
		}

		/*
		save the return value to keep the signal valid
		sig_name : required, unique, used to bind sig-slot
		*/
		template<typename Signature>
		auto AddSignal(std::string sig_name) -> std::shared_ptr<Signal<Signature>>
		{
			auto signal = std::make_shared<Signal<Signature>>();
			signal->name_ = sig_name;

			std::lock_guard<decltype(lock_)> l(lock_);
			signals_[sig_name] = signal;
			return signal;
		}

		/*
		save the return value as long as you want to keep the connection
		sig_name : required, unique, used to bind sig-slot
		slot_name : optional, for debug purpose
		*/
		template<typename Signature>
		auto Connect(std::string sig_name, std::function<Signature> func, std::string slot_name = "") -> std::shared_ptr<Connection<Signature>>
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			auto sig_iter = signals_.find(sig_name);
			if (signals_.end() == sig_iter)
				return std::shared_ptr<Connection<Signature>>();

			auto signal = sig_iter->second.lock();
			if (nullptr == signal)
				return std::shared_ptr<Connection<Signature>>();

			return std::dynamic_pointer_cast<Signal<Signature>>(signal)->Connect(func, slot_name);
		}
	};
}
